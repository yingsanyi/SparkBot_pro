idf_component_register(SRCS "speaker_hello_main.c"
                       INCLUDE_DIRS "."
                       EMBED_FILES "../assets/nihao.wav"
                       REQUIRES sparkbot_audio)
