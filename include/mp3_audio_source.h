#pragma once
#include "audio_source.h"
#include <minimp3/minimp3.h>
#include <minimp3/minimp3_ex.h>

struct from_file {
	std::string file_name;
};



struct mp3_audio_source {
	mp3_audio_source() = default;


	explicit mp3_audio_source(from_file f);

	auto frames(std::chrono::milliseconds frame_length) {
		//mp3dec
	}
	

private:
	std::vector<std::byte> m_file_data = {};
	mp3dec_t m_mp3d = {};

	void from_file(std::string file_name);
	
};

audio_data audio_from_mp3(std::string file_name);
