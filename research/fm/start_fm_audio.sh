#!/system/bin/sh
echo "Stopping any existing hostless..."
pkill -9 -f tinyhostless 2>/dev/null
sleep 0.5

echo "Starting tinyhostless on PCM 0,4 in background..."
nohup /data/local/tmp/tinyhostless -D 0 -P 4 -p 1024 -n 4 -c 2 -r 48000 > /data/local/tmp/hostless.log 2>&1 &
sleep 0.5

echo "Setting mixer controls..."
/data/local/tmp/tinymix 'FM Link' 'FM On'
/data/local/tmp/tinymix 'LINEIN LINEIN On' '1'
/data/local/tmp/tinymix 'ADCL Mixer LINELL Switch' '1'
/data/local/tmp/tinymix 'ADCR Mixer LINERR Switch' '1'
/data/local/tmp/tinymix 'HPL Mixer ADCL Switch' '1'
/data/local/tmp/tinymix 'HPR Mixer ADCR Switch' '1'
/data/local/tmp/tinymix 'HPL Mixer DACL Switch' '1'
/data/local/tmp/tinymix 'HPR Mixer DACR Switch' '1'
/data/local/tmp/tinymix 'HP HP On' '1'
/data/local/tmp/tinymix 'Headphone Volume' '63' '63'
/data/local/tmp/tinymix 'ADC Left Gain' '127'
/data/local/tmp/tinymix 'ADC Right Gain' '127'
/data/local/tmp/tinymix 'AudioMixer Mixer En' 'On'
/data/local/tmp/tinymix 'AudioMixer SRC3 En' 'On'
/data/local/tmp/tinymix 'AudioMixer CH1 Mixer En' 'On'
/data/local/tmp/tinymix 'AudioMixer CH3 Mixer En' 'On'
/data/local/tmp/tinymix 'AudioMixer RMIX2 En' 'On'

echo "Triggering Broadcom tune again..."
setprop debug.fm.freq 0
sleep 0.2
setprop debug.fm.freq 89100

echo "Current status:"
ps -A | grep -i tinyhostless
/data/local/tmp/tinymix 'HP HP On'
/data/local/tmp/tinymix 'FM Link'
/data/local/tmp/tinymix 'LINEIN LINEIN On'
