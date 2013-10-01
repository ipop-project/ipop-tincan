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
      'target_name': 'ipop-tincan',
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
        'ipop-project/ipop-tap/src/headers.c',
        'ipop-project/ipop-tap/src/headers.h',
        'ipop-project/ipop-tap/src/packetio.c',
        'ipop-project/ipop-tap/src/packetio.h',
        'ipop-project/ipop-tap/src/peerlist.c',
        'ipop-project/ipop-tap/src/peerlist.h',
        'ipop-project/ipop-tap/src/socket_utils.c',
        'ipop-project/ipop-tap/src/socket_utils.h',
        'ipop-project/ipop-tap/src/tap.c',
        'ipop-project/ipop-tap/src/tap.h',
        'ipop-project/ipop-tap/src/translator.c',
        'ipop-project/ipop-tap/src/translator.h',
        'ipop-project/ipop-tap/lib/threadqueue/threadqueue.c',
        'ipop-project/ipop-tap/lib/threadqueue/threadqueue.h',
        'ipop-project/ipop-tap/lib/klib/khash.h',
        'ipop-project/ipop-tincan/src/tincan.cc',
        'ipop-project/ipop-tincan/src/tincanconnectionmanager.cc',
        'ipop-project/ipop-tincan/src/tincanconnectionmanager.h',
        'ipop-project/ipop-tincan/src/xmppnetwork.cc',
        'ipop-project/ipop-tincan/src/xmppnetwork.h',
        'ipop-project/ipop-tincan/src/controlleraccess.cc',
        'ipop-project/ipop-tincan/src/controlleraccess.h',
        'xmpp/jingleinfotask.cc',
        'xmpp/jingleinfotask.h',
      ],
    },  # target ipop-tincan
  ],
}
