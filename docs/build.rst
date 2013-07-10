========================================
Build instructions for Linux and Android
========================================

These instructions are from these links:

https://sites.google.com/a/chromium.org/dev/developers/how-tos/install-depot-tools

https://sites.google.com/site/webrtc/reference/getting-started/prerequisite-sw

https://sites.google.com/site/webrtc/reference/getting-started


Installing tools and code
=========================

Install Oracle Java6
---------------------

Our code does not need Java to compile or run, but the setup tools require it

Download Java6 at 

http://www.oracle.com/technetwork/java/javase/downloads/jdk6downloads-1902814.html

Accept License Agreement, click on jdk-6u45-linux-x64.bin for 64bit Linux to download

1. Do the following to extract Java::

    chmod 755 jdk-6u45-linux-x64.bin
    ./jdk-6u45-linux-x64.bin

Install necessary libraries and chromium tools
----------------------------------------------

1. This works on Debian-based distros::

    sudo apt-get install libnss3-dev libasound2-dev libpulse-dev libjpeg62-dev
    sudo apt-get install libxv-dev libgtk2.0-dev libexpat1-dev git

2. Download depot_tools for chromium repo::

    git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git

3. Set up environmental variables::

    export JAVA_HOME=`pwd`/jdk1.6.0_45
    export PATH="$JAVA_HOME"/bin:`pwd`/depot_tools:"$PATH"

Get the libjingle and socialvpn source code
-------------------------------------------

1. Configure gclient to download libjingle code::

    gclient config http://libjingle.googlecode.com/svn/trunk
    echo "target_os = ['android', 'unix']" >> .gclient


2. Download libjingle and dependencies (this takes a while)::

    gclient sync --force

3. Download socialvpn from github.com/socialvpn::

    cd trunk/talk; mkdir socialvpn; cd socialvpn
    git clone https://github.com/socialvpn/svpn-core.git
    git clone https://github.com/socialvpn/svpn-jingle.git


Building socialvpn
==================

For Linux
---------

1. Return to libjingle trunk directory::

    cd ../../

2. Copy modified gyp files to trunk/talk directory::

    cp talk/socialvpn/svpn-jingle/build/socialvpn.gyp talk/
    cp talk/socialvpn/svpn-jingle/build/libjingle_all.gyp talk/

3. Generate ninja build files::

    gclient runhooks --force

4. Build socialvpn for linux (binary localed at out/Release/svpn-jingle)::

    ninja -C out/Release svpn-jingle

5. To build debug version with gdb symbols (but creates 25 MB binary)::

    ninja -C out/Debug svpn-jingle


For Android
-----------

1. Assuming you have just compiled for Linux for instructions above, move
   binaries to avoid overwriting them::

    mv out out.x86_64

2. Set up android environmental variables::

    source build/android/envsetup.sh
    export GYP_DEFINES="build_with_libjingle=1 build_with_chromium=0 libjingle_java=0 $GYP_DEFINES"

3. Create ninja build files::

    gclient runhooks --force

4. Build socialvpn for android (binary located at out/Release/svpn-jingle)::

    ninja -C out/Release svpn-jingle
