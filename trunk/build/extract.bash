#!/bin/bash
cd $(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
mkdir lib
ar -t ./out/Release/obj/chromium/src/third_party/boringssl/libboringssl.a | xargs ar -rcs lib/libboringssl.a
ar -t ./out/Release/obj/webrtc/system_wrappers/libfield_trial_default.a | xargs ar -rcs lib/libfield_trial_default.a
ar -t ./out/Release/obj/chromium/src/third_party/jsoncpp/libjsoncpp.a | xargs ar -rcs lib/libjsoncpp.a
ar -t ./out/Release/obj/webrtc/base/librtc_base.a | xargs ar -rcs lib/librtc_base.a
ar -t ./out/Release/obj/webrtc/base/librtc_base_approved.a | xargs ar -rcs lib/librtc_base_approved.a
ar -t ./out/Release/obj/webrtc/p2p/librtc_p2p.a | xargs ar -rcs lib/librtc_p2p.a
ar -t ./out/Release/obj/webrtc/libjingle/xmllite/librtc_xmllite.a | xargs ar -rcs lib/librtc_xmllite.a
ar -t ./out/Release/obj/webrtc/libjingle/xmpp/librtc_xmpp.a | xargs ar -rcs lib/librtc_xmpp.a
ar -t ./out/Release/obj/chromium/src/third_party/boringssl/libboringssl_asm.a | xargs ar -rcs lib/libboringssl_asm.a
