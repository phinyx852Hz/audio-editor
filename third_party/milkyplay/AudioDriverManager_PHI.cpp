/*
 *  AudioDriverManager for PHI: replaces MilkyPlay's AudioDriverManager.cpp.
 *  PHI plays the mix through its own audio engine, so the only driver here
 *  is the null one, which PHI advances itself.
 *
 *  Same New-BSD licence as the rest of MilkyPlay.
 */
#include "AudioDriverManager.h"
#include "AudioDriver_NULL.h"

#include <cstring>

AudioDriverManager::AudioDriverManager() : driverList (nullptr), numDrivers (1), defaultDriverIndex (0), enumerationIndex (0)
{
    driverList = new AudioDriverInterface*[1];
    driverList[0] = new AudioDriver_NULL();
}

AudioDriverManager::~AudioDriverManager()
{
    for (mp_sint32 i = 0; i < numDrivers; i++)
        delete driverList[i];
    delete[] driverList;
}

AudioDriverInterface* AudioDriverManager::getPreferredAudioDriver() { return driverList[defaultDriverIndex]; }

AudioDriverInterface* AudioDriverManager::getAudioDriverByName (const char* name)
{
    for (mp_sint32 i = 0; i < numDrivers; i++)
        if (name != nullptr && std::strcmp (driverList[i]->getDriverID(), name) == 0)
            return driverList[i];
    return nullptr;
}

const char* AudioDriverManager::getFirstDriverName() const
{
    enumerationIndex = 0;
    return getNextDriverName();
}

const char* AudioDriverManager::getNextDriverName() const
{
    return enumerationIndex < numDrivers ? driverList[enumerationIndex++]->getDriverID() : nullptr;
}

mp_sint32 AudioDriverManager::getPreferredAudioDriverSampleRate() const { return driverList[defaultDriverIndex]->getPreferredSampleRate(); }
mp_sint32 AudioDriverManager::getPreferredAudioDriverBufferSize() const { return driverList[defaultDriverIndex]->getPreferredBufferSize(); }
