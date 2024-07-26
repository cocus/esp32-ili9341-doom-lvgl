idf_component_register(SRCS i_main.c i_network.c i_sound.c i_system.c i_video.c spi_lcd.c sndhw.c dbopl.c memio.c midifile.c mus2mid.c
                       INCLUDE_DIRS include
                       REQUIRES driver spiffs prboom)
