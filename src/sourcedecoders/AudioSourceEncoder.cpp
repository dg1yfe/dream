/******************************************************************************\
 * Technische Universitaet Darmstadt, Institut fuer Nachrichtentechnik
 * Copyright (c) 2001
 *
 * Author(s):
 *	Volker Fischer, Ollie Haffenden
 *
 * Description:
 *	Audio source encoder/decoder
 *
 ******************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
\******************************************************************************/

#include "AudioSourceEncoder.h"
#include <iostream>
#include <algorithm>


/* Signal (SDC data entity type 9) what the encoder really produces. SBR is
   in use iff the encoder takes 2 x 960 input samples per channel and frame
   (fdk-aac with SBR; faac never does SBR). Parametric stereo is only possible
   with SBR and a stereo input; otherwise the service is plain stereo. */
static void SetEncodedAudioMode(CAudioParam& AudioParam, int iNumChannels,
                                unsigned long lNumSampEncInPerChannel)
{
    if (AudioParam.eAudioCoding == CAudioParam::AC_AAC)
    {
        const bool bSBR = lNumSampEncInPerChannel == 2 * AUD_DEC_TRANSFROM_LENGTH;
        AudioParam.eSBRFlag = bSBR ? CAudioParam::SB_USED : CAudioParam::SB_NOT_USED;
        if (iNumChannels < 2)
            AudioParam.eAudioMode = CAudioParam::AM_MONO;
        else if (!(bSBR && AudioParam.eAudioMode == CAudioParam::AM_P_STEREO))
            AudioParam.eAudioMode = CAudioParam::AM_STEREO;
    }
    else if (AudioParam.eAudioCoding == CAudioParam::AC_OPUS)
    {
        /* Opus (experimental, not part of the DRM standard): the receiver
           (CAudioParam::setFromType9Bits) only accepts SBR flag 0 and audio
           mode "mono" in the SDC; the channel count is carried in the Opus
           packets themselves */
        AudioParam.eSBRFlag = CAudioParam::SB_NOT_USED;
        AudioParam.eAudioMode = CAudioParam::AM_MONO;
    }
    else
    {
        AudioParam.eAudioMode =
                iNumChannels >= 2 ? CAudioParam::AM_STEREO : CAudioParam::AM_MONO;
    }
}


/* Implementation *************************************************************/

CAudioSourceEncoderImplementation::CAudioSourceEncoderImplementation()
    : bUsingTextMessage(false), codec(nullptr), bRateControl(false),
      iEncBitRate(0), iEncBitRateNom(0), iEncBitRateMin(0), iEncBitRateCeil(0),
      iCleanSuperFrames(0), iCeilCleanSuperFrames(0), rPeakFill(0), rCeilPeakFill(0),
      lRcSuperFrames(0), lRcOverflows(0), dRcRateSum(0)
{
    /* Initialize Audio Codec List */
    CAudioCodec::InitCodecList();

    /* In case codec might be dereferenced before initialised this will
       get us a null codec at least, it is safer than other codec */
    codec = CAudioCodec::GetEncoder(AC_NULL);

    /* Needed by TransmDlg.cpp to report available codec */
    bCanEncodeAAC  = CAudioCodec::GetEncoder(CAudioParam::AC_AAC,  true) != nullptr;
    bCanEncodeOPUS = CAudioCodec::GetEncoder(CAudioParam::AC_OPUS, true) != nullptr;
}

CAudioSourceEncoderImplementation::~CAudioSourceEncoderImplementation()
{
    PrintRateControlStats();

    /* Unreference Audio Codec List */
    CAudioCodec::UnrefCodecList();
}

void
CAudioSourceEncoderImplementation::ProcessDataInternal(CParameter& Parameters,
                                                       CVectorEx < _SAMPLE >
                                                       *pvecInputData,
                                                       CVectorEx < _BINARY >
                                                       *pvecOutputData,
                                                       int &iInputBlockSize,
                                                       int &iOutputBlockSize)
{
    int iCurSelServ = 0;

    /* Reset data to zero. This is important since usually not all data is used
       and this data has to be set to zero as defined in the DRM standard */
    for (int i = 0; i < iOutputBlockSize; i++)
        (*pvecOutputData)[i] = 0;

    if (bIsDataService == false)
    {
        /* Check if audio param have changed */
        Parameters.Lock();

        /* Get audio param for audio service */
        CAudioParam& AudioParam(Parameters.Service[iCurSelServ].AudioParam);

        if (AudioParam.bParamChanged)
        {
            AudioParam.bParamChanged = false;
            codec->EncUpdate(AudioParam);
        }
        Parameters.Unlock();

        CAudioParam::EAudCod eAudioCoding = AudioParam.eAudioCoding;
        bool bDrmSf = (eAudioCoding != CAudioParam::AC_MPEGAAC);
        int iCrcBytes = (bDrmSf ? 1 : 0);


        /* Resample data to encoder bit-rate */
        /* Change type of data (short -> real) */
        /* The input data is always stereo, if the encoder
           if set to mono we take the left channel only */
        for (int j = 0; j < iInputBlockSize / 2; j++)
        {
            for (int i = 0; i < iNumChannels; i++)
                vecTempResBufIn[i][j] = (*pvecInputData)[j * 2 + i];
        }

        /* Resample data */
        for (int i = 0; i < iNumChannels; i++)
            ResampleObj[i].Resample(vecTempResBufIn[i], vecTempResBufOut[i]);

        /* Bytes (CRC included) left in this audio super frame, for encoders
           that take a frame size range */
        int iSfRemaining = iAudioPayloadLen + iNumAudioFrames * iCrcBytes;

        /* Split data in individual audio blocks */
        for (int j = 0; j < iNumAudioFrames; j++)
        {
            int bytesEncoded;
            CVector < unsigned char >vecsTmpData(lMaxBytesEncOut);

            /* Convert _REAL type to _SAMPLE type, copy in smaller buffer */
            for (unsigned long k = 0; k < lNumSampEncIn; k++)
            {
                for (int i = 0; i < iNumChannels; i++)
                    vecsEncInData[k * iNumChannels + i] =
                            Real2Sample(vecTempResBufOut[i][j * lNumSampEncIn + k]);
            }

            /* Encoders with exact frame sizes (fdk-aac, libxaac): fill the
               audio super frame exactly. The length of the last frame is
               implicit (remaining payload) and SBR data is read backwards from
               the end of each frame, so no padding may follow a frame. */
            if (bDrmSf && codec->EncHasFrameSizeRange())
            {
                /* A frame may borrow what the earlier frames of this super frame
                   saved, plus half an average frame; every later frame keeps at
                   least half an average frame (and its higher protected part);
                   the last frame takes exactly the rest. */
                const int iAvg = iAudioPayloadLen / iNumAudioFrames + iCrcBytes;
                const int iLeft = iNumAudioFrames - j; /* this frame included */
                const int iMinF = std::max(iAvg / 2, iNumHigherProtectedBytes + iCrcBytes + 1);
                /* a later frame can take at most twice the average (and what the encoder
                   can write), so the rest of the super frame always stays reachable */
                const int iCapF = std::min(2 * iAvg, int(lMaxBytesEncOut));
                int iMax = iSfRemaining - (iLeft - 1) * iMinF;
                int iMin = iMinF;
                if (iLeft > 1)
                {
                    const int iCredit = iSfRemaining - iLeft * iAvg;
                    iMax = std::min(iMax, iAvg + iCredit + iAvg / 2);
                    iMin = std::max(iMinF, iSfRemaining - (iLeft - 1) * iCapF);
                }
                else
                {
                    iMin = iMax = iSfRemaining;
                }
                iMax = std::min(iMax, int(lMaxBytesEncOut));
                iMin = std::min(iMin, iMax);
                codec->EncSetNextFrameSizeRange(unsigned(iMin), unsigned(iMax));
            }
            else if (bDrmSf && codec->EncHasExactFrameSize())
            {
                const int iSlot = iAudioPayloadLen / iNumAudioFrames;
                const int iBytes = (j < iNumAudioFrames - 1) ? iSlot
                        : iAudioPayloadLen - (iNumAudioFrames - 1) * iSlot;
                codec->EncSetNextFrameSize(unsigned(iBytes + iCrcBytes));
            }
            /* Actual encoding */
            bytesEncoded = codec->Encode(vecsEncInData, lNumSampEncIn * iNumChannels, vecsTmpData, lMaxBytesEncOut);
            if (bytesEncoded > 0)
            {
                /* Extract CRC */
                aac_crc_bits[j] = vecsTmpData[0];

                /* Extract actual data */
                for (int i = 0; i < bytesEncoded - iCrcBytes ; i++)
                    audio_frame[j][i] = vecsTmpData[i + iCrcBytes];

                /* Store block lengths for boarders in AAC super-frame-header */
                veciFrameLength[j] = bytesEncoded - iCrcBytes;
                iSfRemaining -= bytesEncoded;
            }
            else
            {
                /* Encoder is in initialization phase, reset CRC and length */
                aac_crc_bits[j] = 0;
                veciFrameLength[j] = 0;
            }
        }

        /* The frames must fit into the audio payload: the receiver rejects
           the whole super frame if the frame borders exceed it, the last
           frame (implicit length) must keep at least one byte, and each
           frame should be at least iNumHigherProtectedBytes long (the
           receiver fills the higher protected part of every frame). Frames
           that do not fit are dropped (zero bytes with a wrong CRC, the
           decoder conceals them) instead of being truncated. Encoders without
           exact frame sizes (faac) are kept below the payload by
           UpdateRateControl(). */
        if (bDrmSf)
        {
            const int iMinLen = std::min(iNumHigherProtectedBytes, int(lMaxBytesEncOut));
            int iUsed = 0;
            int iRequested = 0;
            bool bDropped = false;
            for (int j = 0; j < iNumAudioFrames; j++)
                iRequested += veciFrameLength[j];
            for (int j = 0; j < iNumAudioFrames; j++)
            {
                const int iRest = iNumAudioFrames - 1 - j; /* frames after j */
                const int iReserve = iRest * iMinLen + ((iRest > 0 && iMinLen == 0) ? 1 : 0);
                if (iUsed + veciFrameLength[j] + iReserve > iAudioPayloadLen)
                {
                    veciFrameLength[j] = 0;
                    aac_crc_bits[j] = 0;
                    bDropped = true;
                }
                if (veciFrameLength[j] < iMinLen)
                {
                    /* pad (or replace a dropped frame) with zeros */
                    for (int i = veciFrameLength[j]; i < iMinLen; i++)
                        audio_frame[j][i] = 0;
                    veciFrameLength[j] = iMinLen;
                }
                iUsed += veciFrameLength[j];
            }
            UpdateRateControl(iRequested, bDropped);
        }

        /* Write data to output vector */
        /* First init buffer with zeros */
        for (int i = 0; i < iOutputBlockSize; i++)
            (*pvecOutputData)[i] = 0;

        /* Reset bit extraction access */
        (*pvecOutputData).ResetBitAccess();

        /* Audio super-frame-header */
        int iAudioFrameLength = 0;
        for (int j = 0; j < iNumBorders; j++)
        {
            /* Accumulate audio frame length */
            iAudioFrameLength += veciFrameLength[j];

            /* Frame border in bytes (12 bits) */
            if (bDrmSf) {
                (*pvecOutputData).Enqueue(iAudioFrameLength, 12);
            }
        }

        /* Byte-alignment (4 bits) in case of odd number of borders */
        if ((iNumBorders & 1) && bDrmSf)
            (*pvecOutputData).Enqueue(0, 4);

        /* Higher protected part */
        int iCurNumBytes = 0;
        for (int j = 0; j < iNumAudioFrames; j++)
        {
            /* Data */
            for (int i = 0; i < iNumHigherProtectedBytes; i++)
            {
                /* Check if enough data is available, set data to 0 if not */
                if (i < veciFrameLength[j])
                    (*pvecOutputData).Enqueue(audio_frame[j][i], 8);
                else
                    (*pvecOutputData).Enqueue(0, 8);

                iCurNumBytes++;
            }

            /* CRCs */
            if (bDrmSf)
                (*pvecOutputData).Enqueue(aac_crc_bits[j], 8);
        }

        /* Lower protected part */
        for (int j = 0; j < iNumAudioFrames; j++)
        {

            for (int i = iNumHigherProtectedBytes; i < veciFrameLength[j]; i++)
            {
                /* If encoder produced too many bits, we have to drop them */
                if (iCurNumBytes < iAudioPayloadLen)
                    (*pvecOutputData).Enqueue(audio_frame[j][i], 8);

                iCurNumBytes++;
            }
        }
        if (!bDrmSf)
          iOutputBlockSize = iCurNumBytes * 8;

#ifdef _DEBUG_
        /* Save number of bits actually used by audio encoder */
        static FILE *pFile = fopen("test/audbits.dat", "w");
        fprintf(pFile, "%d %d\n", iAudioPayloadLen, iCurNumBytes);
        fflush(pFile);
#endif
    }

    /* Data service and text message application ---------------------------- */
    if (bIsDataService)
    {
        // TODO: make a separate modul for data encoding
        /* Write data packets in stream */
        CVector < _BINARY > vecbiData;
        const int iNumPack = iOutputBlockSize / iTotPacketSize;
        int iPos = 0;

        for (int j = 0; j < iNumPack; j++)
        {
            /* Get new packet */
            DataEncoder.GeneratePacket(vecbiData);

            /* Put it on stream */
            for (int i = 0; i < iTotPacketSize; i++)
            {
                (*pvecOutputData)[iPos] = vecbiData[i];
                iPos++;
            }
        }
    }
    else
    {
        /* Text message application. Last four bytes in stream are written */
        if (bUsingTextMessage)
        {
            /* Always four bytes for text message "piece" */
            CVector < _BINARY >
                    vecbiTextMessBuf(SIZEOF__BYTE *
                                     NUM_BYTES_TEXT_MESS_IN_AUD_STR);

            /* Get a "piece" */
            TextMessage.Encode(vecbiTextMessBuf);

            /* Calculate start point for text message */
            const int iByteStartTextMess =
                    iTotNumBitsForUsage -
                    SIZEOF__BYTE * NUM_BYTES_TEXT_MESS_IN_AUD_STR;

            /* Add text message bytes to output stream */
            for (int i = iByteStartTextMess; i < iTotNumBitsForUsage; i++)
                (*pvecOutputData)[i] =
                    vecbiTextMessBuf[i - iByteStartTextMess];
        }
    }
}

void
CAudioSourceEncoderImplementation::InitInternalTx(CParameter & Parameters,
                                                  int &iInputBlockSize,
                                                  int &iOutputBlockSize)
{
    int iCurStreamID;

    int iCurSelServ = 0;		// TEST

    Parameters.Lock();

    /* Close previous encoder instance if any */
    CloseEncoder();

    /* Calculate number of input samples in mono. Audio block are always
       400 ms long */
    const int iNumInSamplesMono = (int) ((_REAL) Parameters.GetAudSampleRate() *
                                         (_REAL) 0.4 /* 400 ms */ );

    /* Set the total available number of bits, byte aligned */
    iTotNumBitsForUsage =
            (Parameters.iNumDecodedBitsMSC / SIZEOF__BYTE) * SIZEOF__BYTE;

    /* Total number of bytes which can be used for data and audio */
    const int iTotNumBytesForUsage = iTotNumBitsForUsage / SIZEOF__BYTE;

    if (Parameters.iNumDataService == 1)
    {
        /* Data service ----------------------------------------------------- */
        bIsDataService = true;
        iTotPacketSize = DataEncoder.Init(Parameters);

        /* Get stream ID for data service */
        iCurStreamID = Parameters.Service[iCurSelServ].DataParam.iStreamID;
    }
    else
    {
        /* Audio service ---------------------------------------------------- */
        bIsDataService = false;

        /* Get stream ID and codec type for audio service */
        iCurStreamID = Parameters.Service[iCurSelServ].AudioParam.iStreamID;
        CAudioParam::EAudCod eAudioCoding = Parameters.Service[iCurSelServ].AudioParam.eAudioCoding;

        /* Get encoder instance */
        codec = CAudioCodec::GetEncoder(eAudioCoding);

        /* Total frame size is input block size minus the bytes for the text
           message (if text message is used) */
        int iTotAudFraSizeBits = iTotNumBitsForUsage;
        if (bUsingTextMessage)
            iTotAudFraSizeBits -=
                    SIZEOF__BYTE * NUM_BYTES_TEXT_MESS_IN_AUD_STR;

        int iNumHeaderBytes;

        switch (eAudioCoding)
        {
        case CAudioParam::AC_AAC:
        {
            int iTimeEachAudBloMS = 40;

            iNumChannels = (Parameters.Service[iCurSelServ].AudioParam.eAudioMode==CAudioParam::AM_MONO)?1:2;

            switch (Parameters.Service[iCurSelServ].AudioParam.eAudioSamplRate)
            {
            case CAudioParam::AS_12KHZ:
                iTimeEachAudBloMS = 80;	/* ms */
                iNumAudioFrames = 5;
                lEncSamprate = 12000; // used later to get ratio
                break;

            case CAudioParam::AS_24KHZ:
                iTimeEachAudBloMS = 40;	/* ms */
                iNumAudioFrames = 10;
                lEncSamprate = 24000; // used later to get ratio
                break;
            }

            /* Number of borders, the border of the last frame is not included because
               the audio frame size of the last frame is the frame remaining bytes */
            iNumBorders = iNumAudioFrames - 1;

            /* Calculate the number of header bytes */
            iNumHeaderBytes = (iNumBorders * 12 + 7) / 8;

            /* The audio_payload_length is derived from the length of the audio
               super frame (data_length_of_part_A + data_length_of_part_B)
               subtracting the audio super frame overhead (bytes used for the audio
               super frame header() and for the aac_crc_bits) (5.3.1.1, Table 5) */
            iAudioPayloadLen = iTotAudFraSizeBits / SIZEOF__BYTE - iNumHeaderBytes - iNumAudioFrames /* for CRCs */ ;

            const int iActEncOutBytes = iAudioPayloadLen / iNumAudioFrames;

            /* Requested tools the encoder does not have fall back explicitly
               (SetEncodedAudioMode() signals what is really transmitted) */
            CAudioParam& AudioParam = Parameters.Service[iCurSelServ].AudioParam;
            if (AudioParam.eSBRFlag == CAudioParam::SB_USED && !codec->CanEncodeSBR(eAudioCoding))
            {
                std::cerr << "warning: AAC encoder \"" << codec->EncGetVersion()
                          << "\" has no SBR (HE-AAC): transmitting AAC-LC without SBR" << std::endl;
                AudioParam.eSBRFlag = CAudioParam::SB_NOT_USED;
            }
            if (AudioParam.eAudioMode == CAudioParam::AM_P_STEREO && !codec->CanEncodePS(eAudioCoding))
            {
                std::cerr << "warning: AAC encoder \"" << codec->EncGetVersion()
                          << "\" has no parametric stereo: transmitting plain stereo" << std::endl;
                AudioParam.eAudioMode = CAudioParam::AM_STEREO;
            }

            /* Open encoder instance */
            codec->EncOpen(AudioParam, lNumSampEncIn, lMaxBytesEncOut);
            lNumSampEncIn /= unsigned(iNumChannels);

            /* Calculate bitrate, bit per second (one frame each
               iTimeEachAudBloMS, with or without SBR) */
            const int iBitRate = iActEncOutBytes * SIZEOF__BYTE * 1000 / iTimeEachAudBloMS;
            InitRateControl(iBitRate);
        }
            break;

        case CAudioParam::AC_OPUS:
        {
            /* Set various encoder parameters */
            iNumChannels = (Parameters.Service[iCurSelServ].AudioParam.eOPUSChan == CAudioParam::OC_MONO) ? 1 : 2;
            iNumAudioFrames = 20;
            lEncSamprate = 48000; // used later to get ratio
            Parameters.Service[iCurSelServ].AudioParam.eAudioSamplRate = CAudioParam::AS_48KHZ;	/* Set parameter in global struct */

            /* Number of borders, opus decoder need to know the exact frame size,
               thus the number of borders equal the number of audio frames */
            iNumBorders = iNumAudioFrames;

            /* Calculate the number of header bytes */
            iNumHeaderBytes = (iNumBorders * 12 + 7) / 8;

            /* The audio_payload_length is derived from the length of the audio
               super frame (data_length_of_part_A + data_length_of_part_B)
               subtracting the audio super frame overhead (bytes used for the audio
               super frame header() and the one CRC byte per frame, which the
               Opus encoder delivers in front of each frame like AAC) */
            iAudioPayloadLen = iTotAudFraSizeBits / SIZEOF__BYTE - iNumHeaderBytes - iNumAudioFrames /* for CRCs */ ;

            const int iActEncOutBytes = (int) (iAudioPayloadLen / iNumAudioFrames);

            /* Open encoder instance */
            codec->EncOpen(Parameters.Service[iCurSelServ].AudioParam, lNumSampEncIn, lMaxBytesEncOut);
            lNumSampEncIn /= iNumChannels;

            /* Calculate bitrate, bit per frame */
            const int iBitRate = iActEncOutBytes * SIZEOF__BYTE;
            codec->EncSetBitrate(iBitRate);

            /* Set flags to reset and init codec params on first ProcessDataInternal call */
            Parameters.Service[iCurSelServ].AudioParam.bOPUSRequestReset = true;
            Parameters.Service[iCurSelServ].AudioParam.bParamChanged = true;
        }
            break;

        default:
            /* Unsupported encoder, parameters must be safe */
            lEncSamprate = 12000;
            iNumChannels = 1;
            iNumAudioFrames = 1;
            iNumBorders = 0;
            iNumHeaderBytes = 0;
        }

        /* Set parameter in global struct (keeps parametric stereo, sets the
           SBR flag according to the encoder) */
        SetEncodedAudioMode(Parameters.Service[iCurSelServ].AudioParam,
                            iNumChannels, lNumSampEncIn);

        /* Signal the text message only if its bytes are really reserved at the
           end of the stream (the receiver removes them from the audio payload) */
        Parameters.Service[iCurSelServ].AudioParam.bTextflag = bUsingTextMessage;

        /* Init storage for actual data, CRCs and frame lengths */
        audio_frame.Init(iNumAudioFrames, lMaxBytesEncOut);
        vecsEncInData.Init(lNumSampEncIn * iNumChannels);
        aac_crc_bits.Init(iNumAudioFrames);
        veciFrameLength.Init(iNumAudioFrames);

        _REAL rRatio = _REAL(lEncSamprate) / _REAL(Parameters.GetAudSampleRate()) * _REAL(lNumSampEncIn) / _REAL(AUD_DEC_TRANSFROM_LENGTH);

        for (int i = 0; i < iNumChannels; i++)
        {
            /* Additional buffers needed for resampling since we need conversation
               between _SAMPLE and _REAL */
            vecTempResBufIn[i].Init(iNumInSamplesMono);
            vecTempResBufOut[i].Init(lNumSampEncIn * iNumAudioFrames, 0.0);
            /* Init resample objects */
            ResampleObj[i].Init(iNumInSamplesMono, rRatio);
        }

        /* Calculate number of bytes for higher protected blocks */
        iNumHigherProtectedBytes =
                (Parameters.Stream[iCurStreamID].iLenPartA
                 - iNumHeaderBytes -
                 iNumAudioFrames /* CRC bytes */ ) / iNumAudioFrames;

        if (iNumHigherProtectedBytes < 0)
            iNumHigherProtectedBytes = 0;
    }

    /* Adjust part B length for SDC stream. Notice, that the
       "Parameters.iNumDecodedBitsMSC" parameter depends on these settings.
       Thus, length part A and B have to be set before, preferably in the
       DRMTransmitter initialization */
    if ((Parameters.Stream[iCurStreamID].iLenPartA == 0) ||
            (iTotNumBytesForUsage < Parameters.Stream[iCurStreamID].iLenPartA))
    {
        /* Equal error protection was chosen or protection part A was chosen too
           high, set to equal error protection! */
        Parameters.Stream[iCurStreamID].iLenPartA = 0;
        Parameters.Stream[iCurStreamID].iLenPartB = iTotNumBytesForUsage;
    }
    else
        Parameters.Stream[iCurStreamID].iLenPartB = iTotNumBytesForUsage -
                Parameters.Stream[iCurStreamID].iLenPartA;

    /* Define input and output block size */
    iOutputBlockSize = Parameters.iNumDecodedBitsMSC;
    iInputBlockSize = iNumInSamplesMono * 2 /* stereo */ ;

    Parameters.Unlock();
}

void
CAudioSourceEncoderImplementation::InitInternalRx(CParameter& Parameters,
                                                  int &iInputBlockSize,
                                                  int &iOutputBlockSize)
{

    int iCurSelServ = 0;		// TEST

    Parameters.Lock();

    /* Close previous encoder instance if any */
    CloseEncoder();

    /* Calculate number of input samples in mono. Audio block are always 400 ms long */
    int iNumInSamplesMono = (int) ((_REAL) Parameters.GetAudSampleRate() *
                                         (_REAL) 0.4 /* 400 ms */ );

    /* Set the total available number of bits, byte aligned */
    iTotNumBitsForUsage =
            (Parameters.Stream[0].iLenPartA + Parameters.Stream[0].iLenPartB) * SIZEOF__BYTE;

    /* Total number of bytes which can be used for data and audio */
    //const int iTotNumBytesForUsage = iTotNumBitsForUsage / SIZEOF__BYTE;

    /* Audio service ---------------------------------------------------- */
    bIsDataService = false;

    /* Total frame size is input block size minus the bytes for the text
       message (if text message is used) */
    int iTotAudFraSizeBits = iTotNumBitsForUsage;
    if (bUsingTextMessage)
        iTotAudFraSizeBits -= SIZEOF__BYTE * NUM_BYTES_TEXT_MESS_IN_AUD_STR;

    /* Get encoder instance */
    codec = CAudioCodec::GetEncoder(Parameters.Service[0].AudioParam.eAudioCoding);

    switch (Parameters.Service[0].AudioParam.eAudioCoding) {

    case CAudioParam::AC_AAC:
    {
        int iTimeEachAudBloMS = 40;
        iNumChannels = (Parameters.Service[iCurSelServ].AudioParam.eAudioMode==CAudioParam::AM_MONO)?1:2;

        switch (Parameters.Service[iCurSelServ].AudioParam.eAudioSamplRate)
        {
        case CAudioParam::AS_12KHZ:
            iTimeEachAudBloMS = 80;	/* ms */
            iNumAudioFrames = 5;
            lEncSamprate = 12000;
            break;

        case CAudioParam::AS_24KHZ:
            iTimeEachAudBloMS = 40;	/* ms */
            iNumAudioFrames = 10;
            lEncSamprate = 24000;
            break;
        }

        /* Number of borders, the border of the last frame is not included because
           the audio frame size of the last frame is the frame remaining bytes */
        iNumBorders = iNumAudioFrames - 1;

        /* Calculate the number of header bytes */
        int iNumHeaderBytes = (iNumBorders * 12 + 7) / 8;

        /* The audio_payload_length is derived from the length of the audio
           super frame (data_length_of_part_A + data_length_of_part_B)
           subtracting the audio super frame overhead (bytes used for the audio
           super frame header() and for the aac_crc_bits) (5.3.1.1, Table 5) */
        iAudioPayloadLen = iTotAudFraSizeBits / SIZEOF__BYTE - iNumHeaderBytes - iNumAudioFrames /* for CRCs */ ;

        const int iActEncOutBytes = (int) (iAudioPayloadLen / iNumAudioFrames);

        /* Open encoder instance */
        codec->EncOpen(Parameters.Service[iCurSelServ].AudioParam, lNumSampEncIn, lMaxBytesEncOut);

        lNumSampEncIn /= iNumChannels;

        /* Calculate bitrate, bit per second (one frame each
           iTimeEachAudBloMS, with or without SBR) */
        const int iBitRate = iActEncOutBytes * SIZEOF__BYTE * 1000 / iTimeEachAudBloMS;
        InitRateControl(iBitRate);
    }
        break;
    case CAudioParam::AC_MPEGAAC:
    {
        iNumChannels = (Parameters.Service[iCurSelServ].AudioParam.eAudioMode==CAudioParam::AM_MONO)?1:2;

        switch (Parameters.Service[iCurSelServ].AudioParam.eAudioSamplRate)
        {
        case CAudioParam::AS_12KHZ:
            iNumAudioFrames = 5;
            lEncSamprate = 12000;
            break;

        case CAudioParam::AS_24KHZ:
            iNumAudioFrames = 10;
            lEncSamprate = 24000;
            break;
        }

        /* Open encoder instance */
        codec->EncOpen(Parameters.Service[iCurSelServ].AudioParam, lNumSampEncIn, lMaxBytesEncOut);

        /* Calculate bitrate, bit per second */
        const int iBitRate = 128000;
        codec->EncSetBitrate(iBitRate);

        lNumSampEncIn /= iNumChannels;
        iNumInSamplesMono  = lNumSampEncIn * iNumAudioFrames;

        //iAudioPayloadLen = lMaxBytesEncOut;
        iAudioPayloadLen = iTotAudFraSizeBits / SIZEOF__BYTE;
    }
    break;
    case CAudioParam::AC_OPUS:
    {
        /* Set various encoder parameters */
        lEncSamprate = 48000;
        iNumChannels = (Parameters.Service[0].AudioParam.eOPUSChan == CAudioParam::OC_MONO) ? 1 : 2;
        iNumAudioFrames = 20;
        Parameters.Service[0].AudioParam.eAudioSamplRate = CAudioParam::AS_48KHZ;	/* Set parameter in global struct */

        /* Number of borders, opus decoder need to know the exact frame size,
           thus the number of borders equal the number of audio frames */
        iNumBorders = iNumAudioFrames;

        /* Calculate the number of header bytes */
        int iNumHeaderBytes = (iNumBorders * 12 + 7) / 8;

        /* The audio_payload_length is derived from the length of the audio
           super frame (data_length_of_part_A + data_length_of_part_B)
           subtracting the audio super frame overhead (bytes used for the audio
           super frame header() and the one CRC byte per frame) */
        iAudioPayloadLen = iTotAudFraSizeBits / SIZEOF__BYTE - iNumHeaderBytes - iNumAudioFrames /* for CRCs */ ;

        const int iActEncOutBytes = (int) (iAudioPayloadLen / iNumAudioFrames);

        /* Open encoder instance */
        codec->EncOpen(Parameters.Service[iCurSelServ].AudioParam, lNumSampEncIn, lMaxBytesEncOut);
        lNumSampEncIn /= iNumChannels;

        /* Calculate bitrate, bit per frame */
        const int iBitRate = iActEncOutBytes * SIZEOF__BYTE;
        codec->EncSetBitrate(iBitRate);

        /* Set flags to reset and init codec params on first ProcessDataInternal call */
        Parameters.Service[0].AudioParam.bOPUSRequestReset = true;
        Parameters.Service[0].AudioParam.bParamChanged = true;
    }
        break;

    default:
        /* Unsupported encoder, parameters must be safe */
        lEncSamprate = 12000;
        iNumChannels = 1;
        iNumAudioFrames = 1;
        iNumBorders = 0;
    }

    /* Set parameter in global struct (keeps parametric stereo, sets the
       SBR flag according to the encoder) */
    SetEncodedAudioMode(Parameters.Service[0].AudioParam, iNumChannels, lNumSampEncIn);

    /* Init storage for actual data, CRCs and frame lengths */
    audio_frame.Init(iNumAudioFrames, lMaxBytesEncOut);
    vecsEncInData.Init(lNumSampEncIn * iNumChannels);
    aac_crc_bits.Init(iNumAudioFrames);
    veciFrameLength.Init(iNumAudioFrames);

    for (int i = 0; i < iNumChannels; i++)
    {
        /* Additional buffers needed for resampling since we need conversation
           between _SAMPLE and _REAL */
        vecTempResBufIn[i].Init(iNumInSamplesMono);
        vecTempResBufOut[i].Init(lNumSampEncIn * iNumAudioFrames, (_REAL) 0.0);

        /* Init resample objects */
        ResampleObj[i].Init(iNumInSamplesMono,
                            (_REAL) lEncSamprate / (_REAL) Parameters.GetAudSampleRate() *
                            (_REAL) lNumSampEncIn / (_REAL) AUD_DEC_TRANSFROM_LENGTH);
    }

    /* Calculate number of bytes for higher protected blocks */
    iNumHigherProtectedBytes = 0;

    /* Define input and output block size */
    iOutputBlockSize = iTotNumBitsForUsage;
    iInputBlockSize = iNumInSamplesMono * 2 /* stereo */ ;
    Parameters.Unlock();
}

void
CAudioSourceEncoderImplementation::CloseEncoder()
{
    PrintRateControlStats();
    bRateControl = false;
    if (codec != nullptr)
        codec->EncClose();
}

void
CAudioSourceEncoderImplementation::PrintRateControlStats()
{
    if (lRcSuperFrames > 0)
    {
        std::cerr << "audio rate control: " << lRcSuperFrames << " super frames, "
                  << lRcOverflows << " overflows, mean bitrate "
                  << int(dRcRateSum / lRcSuperFrames) << " of " << iEncBitRateNom
                  << ", final " << iEncBitRate << std::endl;
        lRcSuperFrames = 0;
    }
}

/* Rate control tuning, measured with faac 1.28 (DRM) at 12-20 kbit/s.
   One super frame = 400 ms. Rates in percent/permille of the nominal rate. */
static const int RC_START_PERCENT = 90;       /* initial safety margin */
static const int RC_DOWN_PERCENT = 90;        /* cut on overflow */
static const int RC_CEIL_PERCENT = 90;        /* ceiling after overflow, of the rate that overflowed */
static const int RC_UP_STEP_PERMILLE = 10;    /* step up */
static const int RC_UP_SUPER_FRAMES = 5;      /* clean super frames per step up */
static const _REAL RC_UP_MAX_FILL = (_REAL) 0.95; /* step up only below this fill */
static const int RC_CEIL_SUPER_FRAMES = 150;  /* clean super frames per +1 % ceiling (60 s) */
static const int RC_CEIL_FAST_SUPER_FRAMES = 25; /* ... (10 s) if the fill stayed low */
static const _REAL RC_CEIL_FAST_MAX_FILL = (_REAL) 0.85;

void
CAudioSourceEncoderImplementation::InitRateControl(int iNominalBitRate)
{
    iEncBitRateNom = iNominalBitRate;
    iEncBitRateMin = iNominalBitRate / 2;
    iEncBitRateCeil = iNominalBitRate;
    iCleanSuperFrames = iCeilCleanSuperFrames = 0;
    rPeakFill = rCeilPeakFill = 0;
    lRcSuperFrames = lRcOverflows = 0;
    dRcRateSum = 0;
    /* encoders with exact frame sizes (fdk-aac DRM, libxaac) never overflow */
    bRateControl = !codec->EncHasExactFrameSize();
    iEncBitRate = bRateControl ? iNominalBitRate * RC_START_PERCENT / 100 : iNominalBitRate;
    codec->EncSetBitrate(iEncBitRate);
    if (bRateControl)
        std::cerr << "audio encoder rate control: nominal " << iEncBitRateNom
                  << " bit/s, start " << iEncBitRate << " bit/s" << std::endl;
}

/* Encoders without exact frame sizes (faac) overshoot now and then. Each
   overflow drops a frame, so the rate is cut by 10 % at once and capped
   (ceiling) at that level. The ceiling rises again by 1 % of the nominal rate
   per minute without overflow (every 10 s while the fill stays below 85 %),
   and the rate follows it in small steps while the super frames leave some
   headroom, up to the nominal rate. Unlike the old one-way back-off, a burst
   of overflows (e.g. at start-up) no longer costs bitrate for the rest of the
   session. */
void
CAudioSourceEncoderImplementation::UpdateRateControl(int iRequestedBytes, bool bDropped)
{
    if (!bRateControl || iEncBitRateNom <= 0 || iAudioPayloadLen <= 0)
        return;

    lRcSuperFrames++;
    dRcRateSum += iEncBitRate;

    const _REAL rFill = _REAL(iRequestedBytes) / _REAL(iAudioPayloadLen);
    int iNewBitRate = iEncBitRate;
    const char* reason = "";

    if (bDropped)
    {
        lRcOverflows++;
        iEncBitRateCeil = std::max(iEncBitRateMin, iEncBitRate * RC_CEIL_PERCENT / 100);
        iNewBitRate = std::max(iEncBitRateMin, iEncBitRate * RC_DOWN_PERCENT / 100);
        iCleanSuperFrames = iCeilCleanSuperFrames = 0;
        rPeakFill = rCeilPeakFill = 0;
        reason = "super frame overflow";
    }
    else
    {
        rPeakFill = std::max(rPeakFill, rFill);
        rCeilPeakFill = std::max(rCeilPeakFill, rFill);
        ++iCeilCleanSuperFrames;
        if (iCeilCleanSuperFrames >= RC_CEIL_SUPER_FRAMES
                || (iCeilCleanSuperFrames >= RC_CEIL_FAST_SUPER_FRAMES && rCeilPeakFill < RC_CEIL_FAST_MAX_FILL))
        {
            iEncBitRateCeil = std::min(iEncBitRateNom, iEncBitRateCeil + iEncBitRateNom / 100);
            iCeilCleanSuperFrames = 0;
            rCeilPeakFill = 0;
        }
        if (++iCleanSuperFrames >= RC_UP_SUPER_FRAMES)
        {
            if (rPeakFill < RC_UP_MAX_FILL && iEncBitRate < iEncBitRateCeil)
            {
                iNewBitRate = std::min(iEncBitRateCeil,
                                       iEncBitRate + iEncBitRateNom * RC_UP_STEP_PERMILLE / 1000);
                reason = "headroom";
            }
            iCleanSuperFrames = 0;
            rPeakFill = 0;
        }
    }

    if (iNewBitRate != iEncBitRate)
    {
        std::cerr << "audio encoder bitrate " << (iNewBitRate < iEncBitRate ? "reduced" : "raised")
                  << " to " << iNewBitRate << " (" << reason << ", fill "
                  << int(rFill * 100 + 0.5) << " %)" << std::endl;
        iEncBitRate = iNewBitRate;
        codec->EncSetBitrate(iEncBitRate);
    }
}

void
CAudioSourceEncoderImplementation::SetTextMessage(const string & strText)
{
    /* Set text message in text message object */
    TextMessage.SetMessage(strText);

    /* Set text message flag */
    bUsingTextMessage = true;
}

void
CAudioSourceEncoderImplementation::ClearTextMessage()
{
    /* Clear all text segments */
    TextMessage.ClearAllText();

    /* Clear text message flag */
    bUsingTextMessage = false;
}

