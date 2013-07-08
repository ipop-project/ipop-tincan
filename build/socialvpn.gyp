#
# svpn
# Copyright 2013, University of Florida
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#  1. Redistributions of source code must retain the above copyright notice,
#     this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright notice,
#     this list of conditions and the following disclaimer in the documentation
#     and/or other materials provided with the distribution.
#  3. The name of the author may not be used to endorse or promote products
#     derived from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
# EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

{
  'includes': [
    'build/common.gypi',
  ],
  'targets': [
    {
      'target_name': 'turnsvpn',
      'type': 'executable',
      'dependencies': [
        'libjingle.gyp:libjingle',
        'libjingle.gyp:libjingle_p2p',
      ],
      'sources': [
        'p2p/base/turnserver_main.cc',
      ],
    },  # target turnserver
    {
      'target_name': 'xmpp-test',
      'type': 'executable',
      'dependencies': [
        'libjingle.gyp:libjingle',
        #'libjingle.gyp:libjingle_p2p',
      ],
      'sources': [
        'examples/test/jingle_xmpp_test.cc',
      ],
    },  # target turnserver
    {
      'target_name': 'svpn-jingle',
      'type': 'executable',
      'cflags' : [
        '-Wall',
      ],
      'conditions': [
        ['OS=="android"', {
          'defines': [
            'DROID_BUILD',
          ],
        }],
      ],
      'dependencies': [
        'libjingle.gyp:libjingle_p2p',
        '<(DEPTH)/third_party/jsoncpp/jsoncpp.gyp:jsoncpp',
        '<(DEPTH)/third_party/openssl/openssl.gyp:openssl',
      ],
      'sources': [
        'socialvpn/svpn-core/src/headers.c',
        'socialvpn/svpn-core/src/headers.h',
        'socialvpn/svpn-core/src/packetio.c',
        'socialvpn/svpn-core/src/packetio.h',
        'socialvpn/svpn-core/src/peerlist.c',
        'socialvpn/svpn-core/src/peerlist.h',
        'socialvpn/svpn-core/src/socket_utils.c',
        'socialvpn/svpn-core/src/socket_utils.h',
        'socialvpn/svpn-core/src/tap.c',
        'socialvpn/svpn-core/src/tap.h',
        'socialvpn/svpn-core/src/translator.c',
        'socialvpn/svpn-core/src/translator.h',
        'socialvpn/svpn-core/lib/threadqueue/threadqueue.c',
        'socialvpn/svpn-core/lib/threadqueue/threadqueue.h',
        'socialvpn/svpn-core/lib/hsearch/hsearch.c',
        'socialvpn/svpn-core/lib/hsearch/search.h',
        'socialvpn/svpn-core/lib/hsearch/hsearch_r.c',
        'socialvpn/svpn-jingle/src/svpnjingle.cc',
        'socialvpn/svpn-jingle/src/svpnconnectionmanager.cc',
        'socialvpn/svpn-jingle/src/svpnconnectionmanager.h',
        'socialvpn/svpn-jingle/src/xmppnetwork.cc',
        'socialvpn/svpn-jingle/src/xmppnetwork.h',
        'socialvpn/svpn-jingle/src/httpui.cc',
        'socialvpn/svpn-jingle/src/httpui.h',
        'xmpp/jingleinfotask.cc',
        'xmpp/jingleinfotask.h',
      ],
    },  # target svpn-jingle
  ],
}
