/******************************************************************************\
 *
 * Copyright (c) 2012-2013
 *
 * Author(s):
 *  David Flamand
 *
 * Description:
 *  Opus Codec Parameters Dialog
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

#include <QPushButton>
#include <QString>
#include <QLabel>
#include <QRadioButton>
#include <QButtonGroup>
#include <QLayout>
#include "AACCodecParams.h"
#include "DialogUtil.h"
#include "CWindow.h"
#include "../util-QT/Util.h"
#include "../DrmTransmitter.h"
#ifdef HAVE_LIBFDK_AAC
# include "../sourcedecoders/fdk_aac_codec.h"
#endif

AACCodecParams::AACCodecParams(CSettings& Settings, CParameter& Parameters,
	int iShortID, QWidget* parent, std::function<int()> payloadRate)
	:
	QDialog(parent), Settings(Settings), Parameters(Parameters),
	iShortID(iShortID), bWasVisible(false), bLastPositionValid(false),
	payloadRate(payloadRate), bCanPS(false), RadioButtonChannelsAuto(nullptr),
	LabelModeInfo(nullptr), TimerModeInfo()
{
	setAttribute(Qt::WA_QuitOnClose, false);
	setupUi(this);

    /* "Auto (recommended)" channel mode, first in the row, and a line that
       shows the mode in effect (or a recommendation) */
    RadioButtonChannelsAuto = new QRadioButton(tr("Auto (recommended)"), GroupBoxChannels);
    RadioButtonChannelsAuto->setToolTip(tr("Parametric stereo below %1 kbit/s audio payload "
        "(if the encoder supports it), stereo above").arg(PS_RECOMMENDED_BELOW_BITRATE / 1000));
    if (QBoxLayout* box = qobject_cast<QBoxLayout*>(GroupBoxChannels->layout()))
        box->insertWidget(0, RadioButtonChannelsAuto);
    LabelModeInfo = new QLabel(this);
    LabelModeInfo->setWordWrap(true);
    gridLayout->addWidget(LabelModeInfo, gridLayout->rowCount(), 0, 1, -1);

	/* Recover window size and position */
	CWinGeom s;
	Settings.Get("Codec Dialog", s);
	if (s.iXPos && s.iYPos)
		move(s.iXPos, s.iYPos);

    /* What the active AAC encoder can do: SBR and parametric stereo need the
       (patched) fdk-aac, faac only does AAC-LC */
    const bool bCanSBR = CAudioCodec::EncoderCanSBR(CAudioParam::AC_AAC);
    bCanPS = CAudioCodec::EncoderCanPS(CAudioParam::AC_AAC);
    QString strWebSite(bCanSBR
        ? "https://www.iis.fraunhofer.de/en/ff/amm/prod/audiocodec/audiocodecs/heaac.html"
        : "https://github.com/knik0/faac");
    LabelInfoDescription->setText(bCanSBR ? tr("HE-AAC") : tr("AAC-LC"));
    LabelInfoVersion->setText(QString::fromStdString(CAudioCodec::EncoderVersion(CAudioParam::AC_AAC)));
	LabelInfoWebSite->setText(Linkify(strWebSite));

	Parameters.Lock();
    /* Unsupported tools fall back explicitly */
    if (!bCanSBR && Parameters.Service[iShortID].AudioParam.eSBRFlag == CAudioParam::SB_USED)
    {
        Parameters.Service[iShortID].AudioParam.eSBRFlag = CAudioParam::SB_NOT_USED;
        Parameters.Service[iShortID].AudioParam.bParamChanged = true;
    }
    if (!bCanPS && !Parameters.Service[iShortID].AudioParam.bAutoStereo
            && Parameters.Service[iShortID].AudioParam.eAudioMode == CAudioParam::AM_P_STEREO)
    {
        Parameters.Service[iShortID].AudioParam.eAudioMode = CAudioParam::AM_STEREO;
        Parameters.Service[iShortID].AudioParam.bParamChanged = true;
    }
	/* Channels */
    if (Parameters.Service[iShortID].AudioParam.bAutoStereo)
        RadioButtonChannelsAuto->setChecked(true);
    else switch (Parameters.Service[iShortID].AudioParam.eAudioMode)
	{
    case CAudioParam::AM_MONO:
		RadioButtonChannelsMono->setChecked(true);
		break;
    case CAudioParam::AM_STEREO:
		RadioButtonChannelsStereo->setChecked(true);
		break;
    case CAudioParam::AM_P_STEREO:
        RadioButtonChannelsParametricStereo->setChecked(true);
        break;
    case CAudioParam::AM_RESERVED:
        ;
    }
    /* Sample Rate */
    switch (Parameters.Service[iShortID].AudioParam.eAudioSamplRate)
	{
    case CAudioParam::AS_12KHZ:
        RadioButton12kHz->setChecked(true);
		break;
    case CAudioParam::AS_24KHZ:
        RadioButton24kHz->setChecked(true);
		break;
	}
    /* options */
    CheckBoxSBR->setCheckState(Parameters.Service[iShortID].AudioParam.eSBRFlag?Qt::Checked:Qt::Unchecked);
	Parameters.Unlock();
    if (!bCanSBR)
    {
        CheckBoxSBR->setEnabled(false);
        CheckBoxSBR->setToolTip(tr("Not available: the AAC encoder in use (faac) has no SBR "
                                   "(HE-AAC). SBR needs the patched fdk-aac library."));
    }
    if (!bCanPS)
    {
        RadioButtonChannelsParametricStereo->setEnabled(false);
        RadioButtonChannelsParametricStereo->setToolTip(tr("Not available: the AAC encoder in use "
                                   "(faac) has no parametric stereo. It needs the patched fdk-aac library."));
    }

	/* Connections */
    connect(ButtonGroupChannels, SIGNAL_BUTTONGROUP_CLICKED_INT, this, SLOT(OnButtonGroupChannels(int)));
    connect(ButtonGroupSampleRate, SIGNAL_BUTTONGROUP_CLICKED_INT,		this, SLOT(OnButtonGroupSampleRate(int)));
    connect(CheckBoxSBR, SIGNAL(toggled(bool)),		this, SLOT(OnCheckBoxSBR(bool)));

    ButtonGroupChannels->addButton(RadioButtonChannelsMono, CAudioParam::AM_MONO);
    ButtonGroupChannels->addButton(RadioButtonChannelsStereo, CAudioParam::AM_STEREO);
    ButtonGroupChannels->addButton(RadioButtonChannelsParametricStereo, CAudioParam::AM_P_STEREO);
    ButtonGroupChannels->addButton(RadioButtonChannelsAuto, ID_CHANNELS_AUTO);

    /* the payload rate follows the channel settings of the transmitter */
    connect(&TimerModeInfo, SIGNAL(timeout()), this, SLOT(UpdateModeInfo()));
    TimerModeInfo.start(1000);
    UpdateModeInfo();
    //ButtonGroupChannels->addButton(RadioButtonChannelsSurround, CAudioParam::AM_SURROUND);

    ButtonGroupSampleRate->addButton(RadioButton12kHz, CAudioParam::AS_12KHZ);
    ButtonGroupSampleRate->addButton(RadioButton24kHz, CAudioParam::AS_24KHZ);
}

AACCodecParams::~AACCodecParams()
{
	/* Save window position */
	CWinGeom s;
	if (bLastPositionValid)
	{
		s.iXPos = iLastXPosition;
		s.iYPos = iLastYPosition;
	}
	else {
		QRect WinGeom = geometry();
		if (WinGeom.isValid() && !WinGeom.isEmpty() && !WinGeom.isNull()) {
			s.iXPos = WinGeom.x();
			s.iYPos = WinGeom.y();
		}
	}
	Settings.Put("Codec Dialog", s);
}

void AACCodecParams::reject()
{ 
	GetDialogPosition();
	QDialog::reject();
}

void AACCodecParams::OnButtonGroupChannels(int iID)
{
    Parameters.Lock();
    CAudioParam& AudioParam = Parameters.Service[iShortID].AudioParam;
    AudioParam.bAutoStereo = (iID == ID_CHANNELS_AUTO);
    /* "auto": the encoder sets the mode in effect at its next start */
    AudioParam.eAudioMode = AudioParam.bAutoStereo ? CAudioParam::AM_STEREO
                                                   : static_cast<CAudioParam::EAudioMode>(iID);
	AudioParam.bParamChanged = true;
	Parameters.Unlock();
    UpdateModeInfo();
}

void AACCodecParams::showEvent(QShowEvent* e)
{
    UpdateModeInfo();
    QDialog::showEvent(e);
}

void AACCodecParams::UpdateModeInfo()
{
    if (LabelModeInfo == nullptr || (sender() == &TimerModeInfo && !isVisible()))
        return;
    const int iRate = payloadRate ? payloadRate() : -1;
    Parameters.Lock();
    const CAudioParam AudioParam = Parameters.Service[iShortID].AudioParam;
    Parameters.Unlock();
    const QString strRate = iRate >= 0 ? tr("%1 kbit/s").arg(iRate / 1000.0, 0, 'f', 1) : tr("unknown");
    const bool bLow = iRate >= 0 && iRate < PS_RECOMMENDED_BELOW_BITRATE;
    const CAudioParam::EAudioMode eMode = (iRate >= 0 || !AudioParam.bAutoStereo)
            ? CAudioSourceEncoderImplementation::EffectiveAudioMode(AudioParam, iRate, bCanPS)
            : CAudioParam::AM_RESERVED;
    QString strMode;
    switch (eMode)
    {
    case CAudioParam::AM_MONO: strMode = tr("Mono"); break;
    case CAudioParam::AM_P_STEREO: strMode = tr("Parametric stereo"); break;
    case CAudioParam::AM_STEREO: strMode = tr("Stereo"); break;
    default: strMode = tr("decided when the transmitter starts");
    }
    QString strInfo = tr("In effect: %1%2 (audio payload %3)")
            .arg(strMode, AudioParam.bAutoStereo ? tr(", auto") : QString(), strRate);
    QString strStereoTip;
    if (!AudioParam.bAutoStereo && AudioParam.eAudioMode == CAudioParam::AM_STEREO && bCanPS && bLow)
    {
        strStereoTip = tr("Below %1 kbit/s audio payload parametric stereo sounds better than "
                          "stereo: Parametric stereo or Auto is recommended.")
                .arg(PS_RECOMMENDED_BELOW_BITRATE / 1000);
        strInfo += "<br><b>" + strStereoTip + "</b>";
    }
    LabelModeInfo->setText(strInfo);
    RadioButtonChannelsStereo->setToolTip(strStereoTip);
}

void AACCodecParams::OnButtonGroupSampleRate(int iID)
{
	Parameters.Lock();
    Parameters.Service[iShortID].AudioParam.eAudioSamplRate =  static_cast<CAudioParam::EAudSamRat>(iID);
	Parameters.Service[iShortID].AudioParam.bParamChanged = true;
	Parameters.Unlock();
}

void AACCodecParams::OnCheckBoxSBR(bool checked)
{
	Parameters.Lock();
    Parameters.Service[iShortID].AudioParam.eSBRFlag = checked?CAudioParam::SB_USED:CAudioParam::SB_NOT_USED;
	Parameters.Service[iShortID].AudioParam.bParamChanged = true;
	Parameters.Unlock();
}

void AACCodecParams::Toggle(void)
{
	bWasVisible = isVisible();
	if (bWasVisible)
	{
		GetDialogPosition();
		hide();
	}
	else
		show();
}

void AACCodecParams::Show(bool bShow)
{
	if (bShow)
	{
		if (bWasVisible)
			show();
	}
	else {
		bWasVisible = isVisible();
		if (bWasVisible)
			GetDialogPosition();
		hide();
	}
}

void AACCodecParams::GetDialogPosition(void)
{
	QRect WinGeom = geometry();
	if (WinGeom.isValid() && !WinGeom.isEmpty() && !WinGeom.isNull())
	{
		iLastXPosition = WinGeom.x();
		iLastYPosition = WinGeom.y();
		bLastPositionValid = true;
	}
}

