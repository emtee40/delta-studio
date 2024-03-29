#include "../include/yds_audio_device.h"

#include "../include/yds_audio_buffer.h"

ysAudioDevice::ysAudioDevice() : ysAudioSystemObject("AUDIO_DEVICE", API_UNDEFINED) {
	m_connected = false;
	m_windowAssociation = 0;
}

ysAudioDevice::ysAudioDevice(AUDIO_SYSTEM_API API) : ysAudioSystemObject("ysAudioDevice", API) {
	m_connected = false;
	m_windowAssociation = 0;
}

ysAudioDevice::~ysAudioDevice() {
    /* void */
}

ysError ysAudioDevice::DestroyAudioBuffer(ysAudioBuffer *&buffer) {
	YDS_ERROR_DECLARE("DestroyaAudioBuffer");

	if (buffer == NULL) return YDS_ERROR_RETURN(ysError::YDS_INVALID_PARAMETER);

	m_audioBuffers.Delete(buffer->GetIndex());

	return YDS_ERROR_RETURN(ysError::YDS_NO_ERROR);
}

void ysAudioDevice::DestroyAudioBuffers() {
	int count = m_audioBuffers.GetNumObjects();

	for(int i = count - 1; i >= 0; i--) {
		ysAudioBuffer *buffer = m_audioBuffers.Get(i);
		DestroyAudioBuffer(buffer);
	}
}
