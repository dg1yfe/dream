/******************************************************************************\
 *
 * Copyright (c) 2026
 *
 * Description:
 *  Sound "output device" writing a 16 bit stereo WAV file, e.g. the DRM
 *  transmitter signal for a file loopback ("-O file.wav"). Selected by the
 *  sound factory for device names ending in ".wav". Writes are not paced.
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

#ifndef AUDIOFILEOUT_H
#define AUDIOFILEOUT_H

#include "soundinterface.h"
#include "../GlobalDefinitions.h"
#include <cstdio>
#include <cstdint>
#include "../util/AudioFile.h"
#include <set>
#include <string>

class CAudioFileOut : public CSoundOutInterface
{
public:
    CAudioFileOut() : strOutFileName(), iSampleRate(0), bOpen(false) {}
    virtual ~CAudioFileOut() { Close(); }

    static bool IsFileName(const std::string& s)
    {
        const size_t p = s.rfind('.');
        if (p == std::string::npos)
            return false;
        const std::string ext = s.substr(p + 1);
        return ext == "wav" || ext == "WAV";
    }

    virtual void Enumerate(std::vector<std::string>& names,
                           std::vector<std::string>& descriptions,
                           std::string& defaultName)
    {
        names.clear();
        descriptions.clear();
        if (IsFileName(defaultName))
            recent.insert(defaultName);
        for (auto itr = recent.begin(); itr != recent.end(); ++itr) {
            names.push_back(*itr);
            descriptions.push_back("file");
        }
    }
    virtual std::string GetItemName() { return strOutFileName; }
    virtual void SetItem(std::string strFileName)
    {
        if (strFileName != strOutFileName)
            Close();
        strOutFileName = strFileName;
        recent.insert(strFileName);
    }
    virtual CSoundOutInterface* GetItem() { return this; }

    virtual bool Init(int iNewSampleRate, int, bool)
    {
        if (bOpen && iNewSampleRate == iSampleRate)
            return false;
        Close();
        iSampleRate = iNewSampleRate;
        WaveFile.Open(strOutFileName, iSampleRate);
        bOpen = true;
        return true;
    }
    virtual bool Write(CVector<short>& psData)
    {
        if (!bOpen)
            return true;
        for (int i = 0; i + 1 < psData.Size(); i += 2)
            WaveFile.AddStereoSample(psData[i], psData[i + 1]);
        return false;
    }
    virtual void Close()
    {
        WaveFile.Close();
        bOpen = false;
    }
    virtual std::string GetVersion() { return "WAV file output"; }

protected:
    std::string             strOutFileName;
    std::set<std::string>   recent;
    CWaveFile               WaveFile;
    int                     iSampleRate;
    bool                    bOpen;
};

#endif // AUDIOFILEOUT_H
