#!/bin/sh

yourfilenames=`ls ./*.shader`
for eachfile in $yourfilenames
do
   xxd -i "$eachfile" ../vkshaders/"$eachfile".h
done

yourfilenames=`ls ./*.comp`
for eachfile in $yourfilenames
do
   xxd -i "$eachfile" ../vkshaders/"$eachfile".h
done

echo "finish generating shaders"