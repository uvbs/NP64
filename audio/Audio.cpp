#include <windows.h>
#include <stdio.h>
#include "audio.h"
#include "sound_out.h"
#include "circular_buffer.h"
#include "speex_resampler.h"

SpeexResamplerState* spx_state = NULL;
sound_out *out_drv = NULL;
AUDIO_INFO AudioInfo;

#define AI_STATUS_FIFO_FULL	0x80000000		/* Bit 31: full */
#define AI_STATUS_DMA_BUSY	0x40000000		/* Bit 30: busy */
#define MI_INTR_AI			0x04			/* Bit 2: AI intr */

#define BUFFER_SIZE 1024
int buflen;
void Soundmemcpy           ( void * dest, const void * src, size_t count );
DWORD Frequency, Dacrate = 0;

EXPORT void CALL AiDacrateChanged (int SystemType) {
	
	if (Dacrate != *AudioInfo.AI_DACRATE_REG) {
		Dacrate = *AudioInfo.AI_DACRATE_REG;
		switch (SystemType) {
		case SYSTEM_NTSC: Frequency = 48681812 / (Dacrate + 1); break;
		case SYSTEM_PAL:  Frequency = 49656530 / (Dacrate + 1); break;
		case SYSTEM_MPAL: Frequency = 48628316 / (Dacrate + 1); break;
		}
	}
	if (spx_state) speex_resampler_set_rate(spx_state,Frequency,44100);
	else
	{
		//NOTE: Seems that most plugins do audio init here, not InitiateAudio, fuck you zilmar.
		int spx_err;
		out_drv = create_sound_out_xaudio2();
		if (!out_drv) return;
		spx_state = speex_resampler_init(2, Frequency, 44100, SPEEX_RESAMPLER_QUALITY_DESKTOP,  &spx_err);
		if (spx_state == NULL) return;
		out_drv->open(AudioInfo.hwnd,44100,2,BUFFER_SIZE*2, 60);
	}
}

EXPORT void CALL AiLenChanged (void) {
	int len;
	int buffered = 0;
	short insamples[BUFFER_SIZE*2];
	short *in_samps = insamples;
	short outsamples[BUFFER_SIZE*2];
	short *out_samps = outsamples;

	if (*AudioInfo.AI_LEN_REG == 0) { return; }
	*AudioInfo.AI_STATUS_REG |= AI_STATUS_FIFO_FULL;
	buflen = (*AudioInfo.AI_LEN_REG & 0x3FFF8);
	BYTE *buffer = AudioInfo.RDRAM + (*AudioInfo.AI_DRAM_ADDR_REG & 0x00FFFFF8);

	while (buflen > 0)
	{ 
		int todo = min(BUFFER_SIZE - buffered, buflen);
		Soundmemcpy(insamples,buffer,todo);
		buffer += todo * 2;
		buffered += todo;
		buflen -= todo;
		if (buffered == BUFFER_SIZE)
		{
			int insamplecount = buffered;
			unsigned int outsamplecount = buffered;
			while (insamplecount > 0)
			{
				unsigned todo = min(BUFFER_SIZE, insamplecount);
				int res = speex_resampler_process_interleaved_int(spx_state,in_samps,&todo,outsamples,&outsamplecount);
				out_drv->write_frame(outsamples,outsamplecount,true);
				in_samps += todo*2;
				insamplecount -= todo;
			}
		}
	}

	if (buflen == 0) {
		*AudioInfo.AI_STATUS_REG &= ~AI_STATUS_FIFO_FULL;
		*AudioInfo.MI_INTR_REG |= MI_INTR_AI;
		AudioInfo.CheckInterrupts();
	}

}

EXPORT DWORD CALL AiReadLength (void) {
	return buflen;
}

EXPORT void CALL AiUpdate (BOOL Wait) {
}

EXPORT void CALL CloseDLL (void) {
}

void DisplayError (char * Message, ...) {
	char Msg[400];
	va_list ap;

	va_start( ap, Message );
	vsprintf( Msg, Message, ap );
	va_end( ap );
	MessageBox(NULL,Msg,"Error",MB_OK|MB_ICONERROR);
}

EXPORT void CALL DllAbout ( HWND hParent ) {
}

EXPORT void CALL GetDllInfo ( PLUGIN_INFO * PluginInfo ){ 
	PluginInfo->Version = 0x0101;
	PluginInfo->Type    = PLUGIN_TYPE_AUDIO;
	sprintf(PluginInfo->Name,"n64audio 0.001");
	PluginInfo->NormalMemory  = TRUE;
	PluginInfo->MemoryBswaped = TRUE;
}

EXPORT BOOL CALL InitiateAudio (AUDIO_INFO Audio_Info) {
	HRESULT hr;
	AudioInfo = Audio_Info;
	return TRUE;
}

EXPORT void CALL ProcessAList(void) {
}

EXPORT void CALL RomClosed (void) {
}

void initaudio(int srate) {
}

void Soundmemcpy(void * dest, const void * src, size_t count) {
	if (AudioInfo.MemoryBswaped) {
		_asm {
			mov edi, dest
			mov ecx, src
			mov edx, 0		
		memcpyloop1:
			mov ax, word ptr [ecx + edx]
			mov bx, word ptr [ecx + edx + 2]
			mov  word ptr [edi + edx + 2],ax
			mov  word ptr [edi + edx],bx
			add edx, 4
			mov ax, word ptr [ecx + edx]
			mov bx, word ptr [ecx + edx + 2]
			mov  word ptr [edi + edx + 2],ax
			mov  word ptr [edi + edx],bx
			add edx, 4
			cmp edx, count
			jb memcpyloop1
		}
	} else {
		_asm {
			mov edi, dest
			mov ecx, src
			mov edx, 0		
		memcpyloop2:
			mov ax, word ptr [ecx + edx]
			xchg ah,al
			mov  word ptr [edi + edx],ax
			add edx, 2
			mov ax, word ptr [ecx + edx]
			xchg ah,al
			mov  word ptr [edi + edx],ax
			add edx, 2
			mov ax, word ptr [ecx + edx]
			xchg ah,al
			mov  word ptr [edi + edx],ax
			add edx, 2
			mov ax, word ptr [ecx + edx]
			xchg ah,al
			mov  word ptr [edi + edx],ax
			add edx, 2
			cmp edx, count
			jb memcpyloop2
		}
	}
}
