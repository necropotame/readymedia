# ReadyMedia-transcode

readymedia-transcode is a personal development branch of ReadyMedia (formerly known as MiniDLNA)
created by Lukas Jirkovsky for implementing transcode capabilities
in MiniDLNA.

It contains parts of the transcode patch originally presented
by the user Hiero at:
http://sourceforge.net/tracker/index.php?func=detail&aid=3193201&group_id=243163&atid=1121518

__The transcoding work can be found in the "transcode" branch__

## Installation

### Arch Linux

The PKGBUILD for Arch Linux can be found in AUR:
[readymedia-transcode-git](https://aur.archlinux.org/packages/readymedia-transcode-git/)

### Compilation from sources

#### Prerequisites

Prerequisites are the same as in upstream with additional dependency on ImageMagick:

* libexif
* libjpeg
* libid3tag
* libFLAC
* libvorbis
* libsqlite3
* libavformat,libavutil,libavcodec (the ffmpeg libraries)
* **libmagickwand** (part of ImageMagick)

The library names in this list may differ depending on the distribution you use.
Also note that some distributions split libraries and development files into separate packages.

#### Obtaining the source and compiling

The sources can be obtained from the GIT repository. The transcoding support is
in the branch __transcode__. To clone the repository, use the following command:

    git clone -b transcode https://bitbucket.org/stativ/readymedia-transcode.git

The compilation is the standard autotools based compilation:

    ./configure
    make
    make install

## Usage

Before using transcoding functionality of ReadyMedia-transcode, it needs
to be configured in the minidlna.conf.

In this file you can specify which files need to be transcoded
by specifying audio/video codecs, containers or extensions (for images)
and transcoders that are used for transcoding such files.

More detailed description is provided in the minidlna.conf file at
appropriate places. Example transcoding scripts are provided in
the transcodescripts directory in the ReadyMedia-transcode sources.

## Note on RAW image transcoding:

ReadyMedia-transcode uses ImageMagick to obtain necessary information
about image files. ImageMagick uses so-called delegates for files
that ImageMagick cannot open directly.

For processing camera RAW format, it uses ufraw-batch delegate by
default. However, the ufraw-batch is very slow and it may take a few
seconds to process _one_ image.

To speed up the processing considerably, it is possible to alter
the delegate to extract only the embedded preview instead of doing
full processing of the RAW image. To do that, change the dng:decode
delegate in the ImageMagick's delegates.xml to following:

    <delegate decode="dng:decode" command="&quot;ufraw-batch&quot; --silent --create-id=also --embedded-image &quot;--output=%u.jpg&quot; &quot;%i&quot;"/>
