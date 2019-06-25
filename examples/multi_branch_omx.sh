#!/bin/sh

FILEPATH=$1
INSTANCES=$2

FILE_ELEMENT="filesrc location=$FILEPATH"
DECODE_BRANCH=" ! decodebin"
QUEUE="! queue max-size-bytes=0"
CAPS=
ENC_ELEMENT=" ! avenc_mpeg2video"
DEC_ELEMENT=" ! h264parse ! avdec_h264"
CAPS_X="! video/x-h265, profile=main"
ENC_ELEMENT_X=" ! omxh265enc control-rate=constant filler-data=false b-frames=3 target-bitrate=1000 prefetch-buffer=true"
DEC_ELEMENT_X=" ! omxh264dec internal-entropy-buffers=2"
SINK_ELEMENT=" ! autovideosink"

BRANCH1="$FILE_ELEMENT $CAPS_FILTER $DECODE_BRANCH $ENC_ELEMENT $DEC_ELEMENT $SINK_ELEMENT"
BRANCH2="$FILE_ELEMENT $CAPS_FILTER $DECODE_BRANCH $ENC_ELEMENT $DEC_ELEMENT $SINK_ELEMENT"
BRANCH3="videotestsrc ! fakesink"

BRANCH_ENC_DEC_X="queue $DEC_ELEMENT_X $QUEUE $ENC_ELEMENT_X $CAPS_X"
BRANCH_ENC_DEC="queue $DEC_ELEMENT $QUEUE $ENC_ELEMENT $CAPS"

CMD="./gst-n-launch-1.0 "
for i in `/usr/bin/seq 1 $INSTANCES`
do
  CMD+=" -b \"$FILE_ELEMENT ! $BRANCH_ENC_DEC_X ! filesink location=output_${i}.hevc\""
done
echo $CMD
eval $CMD

