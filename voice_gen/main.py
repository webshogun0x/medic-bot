import asyncio
import edge_tts
from pydub import AudioSegment
import os

async def create_medibot_audio(target_seconds: int = 35, output_path: str = "medibot_init.mp3"):
    text = "System initialization done. Please listen to the usage instructions carefully. To Login you will need to place your RFID card on the scanner and wait then you will place you finger on the fingerprint scanner for verification.To Enrol, this should be after you register on the web app and issued an RFID card, scan your card then follow the instructions to register your fingerprint. At the Dashboard, and Analytics screen. To take your SPO2 and Heart Rate place your finger on the sensor beside the fingerprint scanner then click read then save on the analytics screen, to read you BMI you will mount the machine wait for it to adjust to your height and stay still for 5 seconds to maintain your balance then click read then save on the analytics screen. Then Step down. To measure your Diastolic and Systolic Blood Pressure you will need to place your arm on the black cuff and wait for the reading to be done, the reading on the meter will have to be typed in the analytics screen then click save. To verify if you measurements is taken sucessfully login to the web app it will be automatically updated. For any questions please ask the staff for assistance. Thank you for using Medibot."

    
    # 1. Generate TTS with slightly slower rate for a calm, professional tone
    # Voices: en-US-GuyNeural, en-US-JennyNeural, en-US-AriaNeural, en-GB-SoniaNeural
    tts = edge_tts.Communicate(text, voice="en-US-GuyNeural", rate="-15%")
    await tts.save("_temp_tts.mp3")
    
    # 2. Load & measure duration
    audio = AudioSegment.from_mp3("_temp_tts.mp3")
    current_dur_sec = len(audio) / 1000.0
    
    # 3. Add silence to hit exact target duration
    silence_ms = max(0, int((target_seconds - current_dur_sec) * 1000))
    silence = AudioSegment.silent(duration=silence_ms)
    final_audio = audio + silence
    
    # 4. Export as high-quality MP3
    final_audio.export(
        output_path, 
        format="mp3", 
        bitrate="192k",
        parameters=["-q:a", "2"]  # LAME V2 quality
    )
    
    # Cleanup temp file
    if os.path.exists("_temp_tts.mp3"):
        os.remove("_temp_tts.mp3")
        
    print(f"✅ Generated: {output_path} | Duration: {len(final_audio)/1000:.1f}s")

if __name__ == "__main__":
    # Change target_seconds to anything between 30-60
    asyncio.run(create_medibot_audio(target_seconds=10, output_path="medibot_init05.mp3"))