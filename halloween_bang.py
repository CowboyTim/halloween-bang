import subprocess


def create_dfplayer_explosion(input_file="halloween_bang.wav", output_file="0001.mp3"):
    # 1. Audio filter chain for the explosive impact
    audio_filter = (
        "highpass=f=30,"
        "lowpass=f=10000,"
        "equalizer=f=80:width_type=o:width=1.5:g=8,"
        "aecho=0.8:0.88:60|150:0.4|0.2,"
        "acrusher=bits=8:mode=log:aa=1,"
        "compand=0.3|0.3:1|1:-90/-60|-60/-40|-40/-30|-20/-15:6:0:-90:0.1,"
        "volume=3.0dB"
    )

    # 2. FFmpeg command: processes audio AND encodes to standard MP3 (libmp3lame)
    # Forced to 44100Hz stereo/mono compatible with hardware decoders
    cmd = [
        "ffmpeg",
        "-y",
        "-i",
        input_file,
        "-af",
        audio_filter,
        "-c:a",
        "libmp3lame",
        "-b:a",
        "192k",
        "-ar",
        "44100",
        output_file,
    ]

    print(f"Processing and converting {input_file} for DFPlayer Mini...")
    subprocess.run(cmd, check=True)
    print(f"Success! Saved as {output_file}")


if __name__ == "__main__":
    create_dfplayer_explosion()

