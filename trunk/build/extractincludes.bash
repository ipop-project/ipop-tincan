#!/bin/bash
cd $(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
mkdir include
mkdir include/json
mkdir include/webrtc
mkdir include/webrtc/base
mkdir include/webrtc/libjingle
mkdir include/webrtc/libjingle/xmpp
mkdir include/webrtc/libjingle/xmllite
mkdir include/webrtc/p2p
mkdir include/webrtc/p2p/client
mkdir include/webrtc/p2p/base
mkdir include/webrtc/system_wrappers
mkdir include/third_party/
mkdir include/third_party/jsoncpp
mkdir include/third_party/jsoncpp/source/
mkdir include/third_party/jsoncpp/overrides/
mkdir include/third_party/jsoncpp/source/include/
mkdir include/third_party/jsoncpp/source/include/json/
mkdir include/third_party/jsoncpp/overrides/include/
mkdir include/third_party/jsoncpp/overrides/include/json/

cp ./webrtc/*.h ./include/webrtc/
cp ./third_party/jsoncpp/source/include/json/*.h ./include/json
cp ./third_party/jsoncpp/overrides/include/json/*.h ./include/json
cp ./webrtc/base/*.h ./include/webrtc/base
cp ./webrtc/libjingle/xmpp/*.h ./include/webrtc/libjingle/xmpp
cp ./webrtc/libjingle/xmllite/*.h ./include/webrtc/libjingle/xmllite
cp ./webrtc/p2p/client/*.h ./include/webrtc/p2p/client
cp ./webrtc/p2p/base/*.h ./include/webrtc/p2p/base
cp ./third_party/jsoncpp/source/include/json/*.h ./include/third_party/jsoncpp/source/include/json/
cp ./third_party/jsoncpp/overrides/include/json/*.h ./include/third_party/jsoncpp/overrides/include/json/
