import argparse
import subprocess


def convert_for_dfplayer(input_file, output_file="0001.mp3"):
    # FFmpeg command to transcode any audio file into a 
    # DFPlayer-compatible MP3 (44.1kHz, 192kbps constant bitrate)
    cmd = [
        "ffmpeg",
        "-y",
        "-i", input_file,
        "-c:a", "libmp3lame",
        "-b:a", "192k",
        "-ar", "44100",
        output_file,
    ]

    print(f"Converting {input_file} to DFPlayer Mini compatible MP3...")
    subprocess.run(cmd, check=True)
    print(f"Success! Saved as {output_file}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert audio to DFPlayer Mini compatible MP3.")
    parser.add_argument("input", help="Path to the source audio file (e.g., track.mp3)")
    parser.add_argument("-o", "--output", default="0001.mp3", help="Output filename (default: 0001.mp3)")
    
    args = parser.parse_args()
    convert_for_dfplayer(args.input, args.output)
