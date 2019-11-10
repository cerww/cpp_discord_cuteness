#include "client.h"
#include <fstream>


std::string getFileContents_(const std::string& filePath, decltype(std::ios::in) mode = std::ios::in) {
	std::string fileContents;
	std::ifstream file(filePath, mode);
	file.seekg(0, std::ios::end);
	int filesize = (int)file.tellg();
	file.seekg(0, std::ios::beg);
	filesize -= (int)file.tellg();
	fileContents.resize(filesize);
	file.read(fileContents.data(), filesize);
	file.close();
	return fileContents;
}

int main() {
	client c;
	c.on_guild_text_msg = [](const guild_text_message& msg,shard& s) {
		if(msg.content() == "ping") {
			s.send_message(msg.channel(),"pong");
		}
	};
	
	c.setToken(getFileContents_("token.txt"),token_type::BOT);
	
	c.run();
	return 0;
}
