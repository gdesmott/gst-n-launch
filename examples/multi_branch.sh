#!/bin/bash

PATH=$(cd "$(dirname "$1")"; pwd)/$(basename "$1")
FILEPATH=$1
INSTANCES=$2

FILE_ELEMENT="filesrc location=$FILEPATH"
DECODE_BRANCH=" ! decodebin"
CAPS_FILTER=
ENC_ELEMENT=" ! avenc_mpeg2video"
DEC_ELEMENT=" ! avdec_mpeg2video"
SINK_ELEMENT=" ! autovideosink"
BRANCH1="$FILE_ELEMENT $CAPS_FILTER $DECODE_BRANCH $ENC_ELEMENT $DEC_ELEMENT $SINK_ELEMENT"
#BRANCH2="$FILE_ELEMENT $CAPS_FILTER $DECODE_BRANCH $ENC_ELEMENT $DEC_ELEMENT $SINK_ELEMENT"

BRANCH3="videotestsrc ! fakesink"

CMD="./gst-n-launch-1.0"
for i in `/usr/bin/seq 1 $INSTANCES`
do
  CMD+=" -b \"$BRANCH1\""
done

echo $CMD
eval $CMD
