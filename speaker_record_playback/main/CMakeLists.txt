idf_component_register(SRCS "speaker_record_playback_main.c"
                       INCLUDE_DIRS "."
                       EMBED_FILES "../assets/start_record.wav"
                                   "../assets/end_record.wav"
                       REQUIRES sparkbot_audio)
