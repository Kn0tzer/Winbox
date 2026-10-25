#!/usr/bin/env python3
"""Stage the GStreamer decode/element plugins Wine's winegstreamer needs.

Wine's builtin `winegstreamer.so` (both prefixes) links against
`libgstreamer-1.0`, `libgstaudio-1.0`, `libgstvideo-1.0`, `libgsttag-1.0` and
`libgstbase-1.0` — the pure GStreamer core. However, that core alone cannot
demux or decode *any* container or media file. All real demuxing and decoding
work is done by plugins that live in the Debian `gstreamer1.0-plugins-base` /
`-good` / `-libav` packages — which are not part of the wine64/wine32 trees
staged by tools/rootfs64/build-wine64-zip.sh. As a result, every Wine Media
Foundation / DirectShow path (cutscenes in thousands of games) fails with the
gst_element_factory_make() "no element" path instead of playing anything.

This script pins the exact Debian bookworm debs that close that gap and
stages the minimal set of GStreamer plugins plus its exact DT_NEEDED closure
into `gstreamer64/` and `gstreamer32`. The layout mirrors what the browser
runtime expects: everything sits under `usr/lib/<arch>-linux-gnu/` so it is
found by the guest's dynamic linker.

Plugin set: typefindfunctions, playback (decodebin/playbin), libav (the
all-codec plugin built against libav*), avi, isomp4, matroska, flv, ogg,
wavparse, flac, vorbis, audioparsers, audioconvert, videoconvertscale,
audioresample, volume, app, subparse.

Verification (`--check`) walks the same closure as the staging path and
compares every staged file's sha256, so any drift is caught.
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from pathlib import Path

# Debian bookworm packages whose .so contents we stage, pinned by sha256.
# Each entry maps "name_version" -> {"amd64": ..., "i386": ...}.
DEBS = {
    'gstreamer1.0-libav_1.22.0-2': {
        'amd64': 'c7f407f3b3657042f5f7767300d83a32b2803c0a1eedcc7580bdd09a11053017',
        'i386': '879c760f1886ce09a3054dcfa799c240796856caf7247b9fd0ba58b6f36799eb',
    },
    'gstreamer1.0-plugins-base_1.22.0-3+deb12u7': {
        'amd64': 'e1757f06b9b1faa2aa78cbeaa1b196200908bd7ff7767aea397363877d2a530e',
        'i386': '7cd2eb41e12e529ef2f9539ccd4289af19fb340ab63ac8d64671e5650b1c081b',
    },
    'gstreamer1.0-plugins-good_1.22.0-5+deb12u4': {
        'amd64': '778a9f2c4d24e95208a83d8a9ba4f7ebc78578d310f771bf59e7dbe3b9248330',
        'i386': 'ff7b8ca356ea5a4cacd82b379ce4c2ae5f07199bb977463afc9046675704448d',
    },
    'libaom3_3.6.0-1+deb12u3': {
        'amd64': '0d306f250a365d5309f0f616c5327ebe7cb80cc48332dd3a7fe18c036056f874',
        'i386': 'c09156569b5038555374186098f15aebe63c641a0b310c4f8c53be20177a768c',
    },
    'libass9_1%3a0.17.1-1+deb12u1': {
        'amd64': '8bfab4725e6f7d96e076992fe43ec3619a965ac2b5888bf1824c9d3b2080229a',
        'i386': 'b5b28e62c99f3b85da0f66662ad13f5bfd38dcc0e8b6c9a6bafdb10f886a2c8e',
    },
    'libavcodec59_7%3a5.1.9-0+deb12u1': {
        'amd64': 'a1c6cfeefe7437ad3ff999d3dad39edb9cfd4d2068a944243fa4f313470b59b1',
        'i386': '212d6b665eed2cca408b54fc0236f6c116ad8c8ddee27930b068f1dc62b4f92c',
    },
    'libavfilter8_7%3a5.1.9-0+deb12u1': {
        'amd64': '9df19ac34117740af851af569c3df05c16e20a319cedd832eafb9abde950c384',
        'i386': '5a97cf0b14ebebcf6e7ae7785bc0a13aa10b4f73e1df3eebe6a8a34df3466ae2',
    },
    'libavformat59_7%3a5.1.9-0+deb12u1': {
        'amd64': '35216a7786f2bde3c9c00ffa0574ed27c63cb231726aa03ae3664a36946ef01c',
        'i386': '296aa5f4955a91c6c646c2373efff269c8944fd360d0e6de6da1be86179b608c',
    },
    'libavutil57_7%3a5.1.9-0+deb12u1': {
        'amd64': '3e2c42d9e7235c96cb0329dbe3c13377faaa5aba81ac2d6f3cf54fcd978c9a5f',
        'i386': '7fdaf39ecf500ac4be26dd0d427528c5b655b9acf7a1cfafd75eff62345b6563',
    },
    'libblas3_3.11.0-2': {
        'amd64': '9aae8058b8d02b800bdafc5b340614df1442a2d2abbc2d351947c0fdd67aa08a',
        'i386': '8e86f84b94fb6e2b8fdce1b529aaf62f3573abfb48b240a75376abecf35b1779',
    },
    'libblkid1_2.38.1-5+deb12u3': {
        'amd64': '5a083377f29191cafcdf0f91ec124349b4a7895b531556633a136e43c6d53943',
        'i386': '239ced602de059f88a86cde6af38fb0fc6a3193a3937c8e624c9d0c60831d430',
    },
    'libbluray2_1%3a1.3.4-1': {
        'amd64': '41781aa7af257a7c5083b2b9c4564e02a760361ce332dbc1313ffcf6a0958993',
        'i386': '17a029228f0fe1ca2828bd6bdeae950b88b231e673b177ea840214939450309f',
    },
    'libbrotli1_1.0.9-2+deb12u1': {
        'amd64': 'e27b3d655fa6fadd7a56f706687e94573c0af68b7d6df394a32a381a3a6c6d8b',
        'i386': 'c776b4551da12a8f0f11fe73c443ea6dcbe6818fbe3e25c951161d31c33a11ad',
    },
    'libbs2b0_3.1.0+dfsg-7': {
        'amd64': 'f8629228d3aab164423b2e4304ac82787157a240ec2f3b33eebd31236322dcca',
        'i386': '5f5f7eb6294c3e18269ed57ecc889679606a8ce64263fdecd66db4e9e1994ff1',
    },
    'libc6_2.36-9+deb12u14': {  # amd64-only closure
        'amd64': 'ba4f88f73dbc3ae9055f3c20f4523bfdbaf1ad13ff95e258924f77d20b4fbedf',
    },
    'libcairo-gobject2_1.16.0-7': {
        'amd64': '5898b4c1093ff40bade7acb92affc4a5b5b76a1737cd41cfa2a09cdc2d864d4c',
        'i386': 'bfc9c1614bbe318ecb4dde8185b25c5b3b887c9eb7d105145ac2bd41d85a1d5f',
    },
    'libcairo2_1.16.0-7': {
        'amd64': '927fa059f8b5791d0cd744d4e3a8c57fa872ef849ce14434dc2222ac8a076a69',
        'i386': 'f20f81f188864a3e21b496a12008d56c91802414d36cefb74dd0e20895c97c8c',
    },
    'libchromaprint1_1.5.1-2+b1': {
        'amd64': '2008659c3790890782feaa531134c5b20d96f9ad4a71d519b9de4450ff704fa7',
        'i386': '046aa9c7c97c4b007d23c4040a0a4de1da4ca30dd5615c5052b15c92f9521463',
    },
    'libcjson1_1.7.15-1+deb12u4': {
        'amd64': 'ae043c62e2740072f3c5cc0324ec36659331208f2155de74c89a24801627338c',
        'i386': '8584b3f48610ac6c663c4f6db96a7d8336d1d33e3a43b0bc5a480605bcab9697',
    },
    'libcodec2-1.0_1.0.5-1': {
        'amd64': '5f91673bc70e10496389d061a7f01f27f7f0d9090de56cce7ba21b9c461163d9',
        'i386': 'c4e23b47fdc571b1114512cef327a817459cfc834b537adde2ba47715a626b3b',
    },
    'libcom-err2_1.47.0-2+b2': {
        'amd64': 'a2bac7015e78fbc0c504df3e441f3a22292f1d2d77f9ccda760057f3690e22f2',
        'i386': 'ccdcdc4211cd85ba9b70f8618c9ba42867254086f0d5c8ef7e257272b1db46a3',
    },
    'libdatrie1_0.2.13-2+b1': {
        'amd64': 'f021f193384929989b2dfd19f606a8cebe54b5f209fe387fc40683e810e01ebe',
        'i386': 'f3333ceab99d9eaf3d9e8289637c19ab59d77c7cdecdf33c0f33889015acd7e3',
    },
    'libdav1d6_1.0.0-2+deb12u1': {
        'amd64': '197f08108242177aeae4c04aac11825c39b7d18598191ab769f910687ccb387f',
        'i386': '5ca851c78211f456f25ad98a2011e9e9c631e3591e1a6032b457fa88ec55de75',
    },
    'libdrm2_2.4.114-1+b1': {
        'amd64': 'be18fb670797ba32da9628cf3e8acd83160d8db8c8dd842501dd8e401c3b5371',
        'i386': '33846903c53e3c1a13ae366437286a9e019917f2f3467648ca510d8af31fb59e',
    },
    'libepoxy0_1.5.10-1': {
        'amd64': '050cbedc9c2a5cce976151c720a3817f2103a01e6ac255bdd6f2c6a7509b04c3',
        'i386': '4b221c1801ecfc629da7b22a36dbd553aae9d1b689221477514f094d5d911aa0',
    },
    'libexpat1_2.5.0-1+deb12u5': {  # amd64-only closure
        'amd64': 'cae5ded666d8573dd33060bc66fbac21eea8b101a6b56e2af8bd590fcfa65d79',
    },
    'libflite1_2.2-5': {
        'amd64': 'bfa8c591f1b47730b30b372ec38e02918a1c9795eada67684e1746390ad2f061',
        'i386': '55f838d833214214a9618b18c664ecc0090f22e1224815e1205ac70f785a7c58',
    },
    'libfontconfig1_2.14.1-4': {  # amd64-only closure
        'amd64': '16ee38d374e064f534116dc442b086ef26f9831f1c0af7e5fb4fe4512e700649',
    },
    'libfribidi0_1.0.8-2.1': {
        'amd64': '87fce56627aab7b2968501d370aa3ed6d1c792119efa765e71a690bdfe570e62',
        'i386': '26edb54d5fb29820c328ca63ce597b1b6bc10439716785ab0e02412c1e9bf1b3',
    },
    'libgdk-pixbuf-2.0-0_2.42.10+dfsg-1+deb12u4': {
        'amd64': '19673127f2530deb7cf637c688a4e16384e6887627296e16b8169038928cadc3',
        'i386': '5bc4a2835fb9ee225d061d05e646e78cadf0f8d4849d9fa362b2a38a4b209dca',
    },
    'libgfortran5_12.2.0-14+deb12u1': {
        'amd64': '63d38775d20c425c1f00914b52167ae0e3b4639facdb0aa734877279fb716fbb',
        'i386': '8520508b4d2fea0ff5b56b7ab39350b34b041cdee956eb8024c6c076a0f2a32e',
    },
    'libglib2.0-0_2.74.6-2+deb12u9': {
        'amd64': '7ff85197685d89e150e342b29a59aab1beee400050ba7da73de81cd999ffee5a',
        'i386': 'b29fac9486146be26bc1bc9bacd2d0ba86874856594fa2bba84a3e395f880d68',
    },
    'libgme0_0.6.3-6': {
        'amd64': '3c6fc5488f702cc728ff25420cabd33688861de435e8a188aee6b75d8ed423bd',
        'i386': 'f1ca59e29d5eeeb500640804ebb1794445bd9073086c5bde9f2b1840d355d3ae',
    },
    'libgomp1_12.2.0-14+deb12u1': {
        'amd64': '48fec46bda7f5b1638b9e959889bfbc20491247d402d120bb152687eb48143d7',
        'i386': '16a407d1a59a5f5d9bc93a906e25701f0b1c342f438f5d5f2ef11ed89f7fb646',
    },
    'libgraphite2-3_1.3.14-1+deb12u1': {
        'amd64': 'c19a7f6ba9298db7eef041ae27b08985f2c02009e418063f8bccdb5bc5e858dc',
        'i386': '601da445354ab8c02199ce304a829d29af5157d072f37b435e33788101b92a6c',
    },
    'libgsm1_1.0.22-1': {
        'amd64': '1116dc3dbd0f5e7f6becebd5d5a9a9cbd40fc268f7a3e3f6863dd4a26627627a',
        'i386': '44ed296d70cf04f6f6b69405db87c470ea4d43998f5adfae5f1664a28148f5b2',
    },
    'libgssapi-krb5-2_1.20.1-2+deb12u5': {
        'amd64': 'd8b87aea91b956b416c01a28c44ff6228315ae3b73de40acddac3dc5043607d2',
        'i386': '40da2c7c127d6b3c17ae8990270e7e5b3fd25e78562e12d5806512f9cf200f52',
    },
    'libgstreamer-plugins-base1.0-0_1.22.0-3+deb12u7': {
        'amd64': 'e602ec57f366fbb5dc82c06cf0acd44f99dd60abdbb715da15331859a15e5e8b',
        'i386': '6bbfbaa488e06d3fc909c5cd3ca4f481368e8dcde58844894893a5bb989b39aa',
    },
    'libharfbuzz0b_6.0.0+dfsg-3': {
        'amd64': 'bfce132b7ee67b9c2d2166075b1936a25c8cc6866b6a049f99b8e94baa916e71',
        'i386': '5a997711a272459b6c174fb5a6a0b6d2ded8022ae070fad9f1b8bb226f16017e',
    },
    'libhwy1_1.0.3-3+deb12u1': {
        'amd64': '58db3098074f192d32784cf2d070f82d4e26b10d54bc723830a6b9a43be5013b',
        'i386': 'be4a388141bc533c98c915104839a9e3c6d521ce0c6e2277f58bea5f4bc5d2a7',
    },
    'libicu72_72.1-3+deb12u1': {
        'amd64': 'f7f6f99c6d7b025914df2447fc93e11d22c44c0c8bdd8b6f36691c9e7ddcef88',
        'i386': '4c64bdd09608dfa525f41f5988f371c32a88eab33da425c83a6d41d5b4c95501',
    },
    'libjpeg62-turbo_1%3a2.1.5-2': {
        'amd64': '95ec30140789a342add8f8371ed018924de51b539056522b66f207b25cba9cad',
        'i386': '3c42ca19e27311d692eb97df6b42f1cedc5543896ad428562c4c226932726c00',
    },
    'libjxl0.7_0.7.0-10+deb12u1': {
        'amd64': '19e747c8b509af924d5d2b1602e9eb68ff60a93576e07e0dfd55ca1f857fdf65',
        'i386': 'f9296e56ca5ba43dba5b40e693965ca11e31dbae3eb7cda5924be836562703c9',
    },
    'libk5crypto3_1.20.1-2+deb12u5': {
        'amd64': '7b6e47d15c5c9cdcd456137533d8308aeba608a54cec73b28bd59bfc4b54cf06',
        'i386': '2badc281ffa1dc716a701413e01c48ee2b17c73d1e194ee2228b11e6200f4278',
    },
    'libkeyutils1_1.6.3-2': {
        'amd64': 'cfac89e6a7a54ff3c6a4f843310e25efeddaa771baeae470bd98bd588c373563',
        'i386': '124ebf5b8307e2182afaaa166f057144ccc7132e2391164bd86d6c493efc94ef',
    },
    'libkrb5-3_1.20.1-2+deb12u5': {
        'amd64': 'c90801be09701af221b54dd502a59cb378b7284ff31e480738baca317d2f7653',
        'i386': '1f70267f4e6a623efc785dcd24427692afb499951c058d24db95540003b99468',
    },
    'libkrb5support0_1.20.1-2+deb12u5': {
        'amd64': '8b9c05f2d8d461a10d657b118f572af5bf8fd6c8f93dcef81e852c5ab0f428ab',
        'i386': '8d51bd1c6fa9d3546b28b5c2215eb92cd5fe2085179c37347622b7c5f580ef98',
    },
    'liblapack3_3.11.0-2': {
        'amd64': '4cb4ba9c36ee0242d91c8bcd69286db59a6a0c86f8c522b72f5eb0053e127d7d',
        'i386': '1d4247dc198f71e87867e31765a5f71dfc6e4649bf0f44674e92ea1b0d6ddcf4',
    },
    'liblcms2-2_2.14-2+deb12u1': {
        'amd64': '333efe13d165b8a98194d95e72fd10b502ebbee4dc2eb43fc2ab83e2b53e5f17',
        'i386': 'a9a09b3e455c2fde0f7e0454966a71864b2e3f7f05f04ce662e4fe96cc6a28e0',
    },
    'liblilv-0-0_0.24.14-1': {
        'amd64': 'a28222c34d51cd28f69af9fc95eab77369b4daf98ae35328407864661e72a3d0',
        'i386': '96fe7dd995663ba5ca5dc52fd3119de66324b4bcbe3c8b29f0bab8092b9a3d43',
    },
    'libmbedcrypto7_2.28.3-1': {
        'amd64': 'af3295f80abf7e6ba2cba419134e29bde04192bf3614622ee53ce49de8d02abe',
        'i386': '9f187a1fa6408c24a29b340b80e825eb5c7101d39a73f0e56c43d15f84baf028',
    },
    'libmfx1_22.5.4-1': {  # amd64-only upstream (bookworm)
        'amd64': '19754c2ad5cf13015ebe8e1bd7fbc412ca410d810c036b167ba2b0d9bee36926',
    },
    'libmount1_2.38.1-5+deb12u3': {
        'amd64': 'f46e2d6b006951825adde9757790b6609d7c66c7700f9b68f3db913eaac323c3',
        'i386': 'fea906e66b4b5c2f3cd6b3563f813e3cf7b3e007f21fd14101a99ab9f2451efb',
    },
    'libmysofa1_1.3.1~dfsg0-1': {
        'amd64': 'c4bd2b6d4352be2c0595974e4f62c2d16cfcde83b722be8fc96c0aee3e49c904',
        'i386': '4742cef6869cfce188a752f5b82cf606e75b4abc0ea01358a9dc138b7e7aef22',
    },
    'libnorm1_1.5.9+dfsg-2': {
        'amd64': 'a6388c8d460e86c7b76b08e82ecb09894d1d9c53b488850fb39702af85b6d159',
        'i386': 'ae7519898cec0277161fe039b3aadb35246a8d9a164706ec6f068061cd650c94',
    },
    'libnuma1_2.0.16-1': {
        'amd64': '639e1ab6bd66ead40db8a22c332d7199679fa22db261cac34444eb8eb4c17dda',
        'i386': '659aafdccfbcf061f79c8bc0b3658b0c57cff0d5117fb06ad82937403b7e99e9',
    },
    'libopenjp2-7_2.5.0-2+deb12u3': {
        'amd64': 'a127e2f1741d66bdddb6393a718e3cbec327d53f88c89b859b1f8d73f64cf635',
        'i386': '6d09fac116fff0ed94dd20f8f91e1319d2b2b188e075812aaa97c2b39c628bda',
    },
    'libopenmpt0_0.6.9-1': {
        'amd64': '69c1de4890044359551e4874b230f7ff8b0568687988582ba996d41af602d711',
        'i386': '324e0d53618d339ca77e6a1c6bb2c504edbc4a9ae86f4dea23df38ffda22969e',
    },
    'libpango-1.0-0_1.50.12+ds-1': {
        'amd64': '851720de07441ae6bb6a7f51fc0f2edb4db7aa6f25b5bf1bf7b72dcab8947b7f',
        'i386': '786aa29d90d330fd5cd7f10e145d77b3bdf75175ed895afe1efbc8fb7836bd8e',
    },
    'libpangocairo-1.0-0_1.50.12+ds-1': {
        'amd64': '96695f9cdd3bf346e3555cf0aa158e23a36114d6dec203b00ab4e93f1b77aab9',
        'i386': '7462f08820dc4d81ea873eea4cc9c5a5cb6a344ed85255a4c697fb676c38237d',
    },
    'libpangoft2-1.0-0_1.50.12+ds-1': {
        'amd64': '78da3f494109f6e7a39c4626aaae7571c600c5854cecda0bc0c902224986a63b',
        'i386': 'afdd5026de7976dc86043117c4b515e15566da2a45ee3093231be7e5c4e048ac',
    },
    'libpgm-5.3-0_5.3.128~dfsg-2': {
        'amd64': '3f124acd98fb6d9d78dff583061736bcb738d102f3bd1e0afca4c0f0435534af',
        'i386': '0014d1ddbd87d20ebb91730f569579154a78c6387d96ec7f1fe182bdb3dc386f',
    },
    'libpixman-1-0_0.42.2-1': {
        'amd64': '9ed75ef81b6b1eba9e1357f8b0ce01d31112b0e07b2b685bee2cf627a8932892',
        'i386': 'bc4bf4f18077e520d676c7fd18b4ae61a39617ca0aba6af4fd2aba735c1f13d0',
    },
    'libplacebo208_4.208.0-3': {
        'amd64': 'f6ecc008ec62532ae51510fba32d388eb8d8bdef56ae6f11eb2511d58edcabd5',
        'i386': '5aef64ca7a9d99872b005a0d3579029869e459bc78e80fa41539d39ea4c5c6cf',
    },
    'libpng16-16_1.6.39-2+deb12u6': {  # amd64-only closure
        'amd64': '940b91d40d51c825de7f7aa133aecff1ffa25a91f12cb6aa6926c9ad55cd640c',
    },
    'libpocketsphinx3_0.8+5prealpha+1-15': {
        'amd64': '6236ee6b8e542cc55e6da15f00e56ff06ef743d7e91c57e0180d07c642b0bc2e',
        'i386': 'f94a9e5d8ccb92fd6e792b3d7a1b4d5e8be1896f426cbd9862a9a3eb5de36828',
    },
    'libpostproc56_7%3a5.1.9-0+deb12u1': {
        'amd64': 'c6ea1be8d11ee56ae2c9ae749edc2f38a3baaa949f882affdb859e1f69e6a168',
        'i386': '823d5b8b4390e478acae0e7af759cb34a615493ab9ea96bfea7d3557b79f7907',
    },
    'libquadmath0_12.2.0-14+deb12u1': {
        'amd64': '3f0c9a4c730da2fdab001cae596625438598fd2996d29d888feeb4c1cdc9011f',
        'i386': '2368fd10e4ab5624964cb0606b19c2b21e0f8cfad1581096fc59d352f223ee89',
    },
    'librabbitmq4_0.11.0-1+deb12u3': {
        'amd64': 'da461d734bf8a1d6a6d9c20e5d68d552aac7e87aa75274c7766c81bb0ca79017',
        'i386': '63c0555da6a5667cb54125b6fcb4c77abc54c86ec46e8eff20e300a63fa27d55',
    },
    'librav1e0_0.5.1-6': {
        'amd64': 'c266adb3545b0b8ff6450dbd09f85f19361bf5bc9290ddf2e869f040cb9725b7',
        'i386': '96feeab327bb2c2d522d04e667312375b1213c6df5c5aa2b7822b0da3134a7b7',
    },
    'librist4_0.2.7+dfsg-1': {
        'amd64': 'dddb27f19810bc0bfe8e0d252e3a5c0dbf43bd7f2996756f42e2833bed6dbe5e',
        'i386': 'ec45c6bf1383bb07c7f284d91c5c530c1662da19d77c1066b6a473fc2a3e06b7',
    },
    'librsvg2-2_2.54.7+dfsg-1~deb12u1': {
        'amd64': '090f2ffbedb57727ee9b264e6ea6e66fe09773629739dd406931264b1bf14dc8',
        'i386': '19bbbfd44c24a25960b826ca92aba40088c076a74251a688fab0844817dd97c9',
    },
    'librubberband2_3.1.2+dfsg0-1': {
        'amd64': '8bc1426b30a522886ce936b4f6b12c05dcd953b092d263cd58f20dd73fae4d74',
        'i386': '9d67ac68a151c07ca93dece1ead936bbad863ff6e56cf5eebf5b7152d121df4e',
    },
    'libselinux1_3.4-1+b6': {
        'amd64': '2b07f5287b9105f40158b56e4d70cc1652dac56a408f3507b4ab3d061eed425f',
        'i386': '1af838cdb8342802e3fdbf334f75c42d3027246bcec72f2c2ca26a996014f143',
    },
    'libserd-0-0_0.30.16-1': {
        'amd64': '9fcf641b2e5e2b6705638a735692ad78c2e55bf4452636da1de80df17cd52fc8',
        'i386': '17434cef3c8ef30ccbb60c96e863b366d236224bc4bfa969fbdc2705186686a7',
    },
    'libshine3_3.1.1-2': {
        'amd64': '6b09f0d577f515feffc071048218a26cdde5346d6e2661f861009897db0204d2',
        'i386': '5cbb5e4e204ab53438839a0b8b01744187b64688b037f6d216e6039432986c60',
    },
    'libsnappy1v5_1.1.9-3': {
        'amd64': 'a2ec50d0f92864e7524c96352bc2c62221126ee1ef58f77af7bf369c60b6e98f',
        'i386': '231b389ecf112c439ff6d62454eea82c0a7ed4519d48bac49ec94e5569c042e6',
    },
    'libsodium23_1.0.18-1+deb12u1': {
        'amd64': 'bceae6943d6e11fb53ec3f81d9630ec9ed03e51f89364902b05ebe402ae946ec',
        'i386': 'ab317aa4cc74ccedf1cfdec4bbc3445c6f47825300dd046d410d6fca04359f6e',
    },
    'libsord-0-0_0.16.14+git221008-1': {
        'amd64': 'aa791ccd64deb45a39eba2798904b8ae93e3454d59a0fd1d1cf4da7577f0e6f7',
        'i386': 'fd2c9aefd453e7e4eb9e8187efb5f752aad124ddd4e116cdc80380fdc5f438f4',
    },
    'libsoxr0_0.1.3-4': {
        'amd64': '73a9f1a290da86562333430517c3f44282f17fcbeefcc2ba7f8abbc2e686f6f3',
        'i386': '8c61b99ec5448bc013672a3ba615d8d04c41b74e0c2d98024344e34fecf18e54',
    },
    'libspeex1_1.2.1-2': {
        'amd64': '0b6f64c60624c87dcd12e58fae56882355908822d077a718022aad888ade0e1e',
        'i386': 'aab86f940453c6df5b0b3c0df33f4ded8620697671efd38e5f8839d6482b3146',
    },
    'libsphinxbase3_0.8+5prealpha+1-16': {
        'amd64': '53d6c1be6a6ee167741f2a31861ad0f2a41c1bc4eef9405e1773d7c22b03ad7f',
        'i386': 'f8b1eb422f6072860ebe0634c727a32edd93f57a116d29e5220cfa8bb053bdd3',
    },
    'libsratom-0-0_0.6.14-1': {
        'amd64': 'd17581d7c34d01cdf1407f2eef63994ef6fdf8522bd9a06ca286ca6090e13b28',
        'i386': 'c69e24960aba80ffa791abb104df698e92a18adc043755d833cb95d0ed24d353',
    },
    'libsrt1.5-gnutls_1.5.1-1+deb12u1': {
        'amd64': '7a785222cd985b93b675252c0a3510cfcd920f0d531ab674fd4b4878d6646581',
        'i386': 'f44a20f88c6140261407ebbe2fdef8e77d3c0cfcf18c17fbe1ca9823d4f84b85',
    },
    'libssh-gcrypt-4_0.10.6-0+deb12u3': {
        'amd64': 'caf4dad1cf601bf467615606f0f126333902e48cc019f7d9d51d3b061d6bea4d',
        'i386': 'c81aeda12621cbd318e20d9f4504e2661cccd277e72abe9ec3d3157fa7b7d586',
    },
    'libssl3_3.0.22-1~deb12u1': {
        'amd64': 'f0a8aa8429209e556c278a9936bbd5f7d2cdb9f7e4e23b1e43ed399217ba80c1',
        'i386': 'de199ad1423e0f72522a0b23f6979dc025a84b2653c15bbebfd4333d0e49bfec',
    },
    'libstdc++6_12.2.0-14+deb12u1': {
        'amd64': '5cd3171216d4ab0fc911cfe9c35509bf2dd8f47761c43b7f6a4296701551a24d',
        'i386': 'a262f9ad4e4b4adcb6a2663051e36e6c117159b048ae7ce8d4af215e90de3e5e',
    },
    'libsvtav1enc1_1.4.1+dfsg-1': {
        'amd64': 'e0f6e357f327e80f26438dcda9c9304c43e2f3343359c6a5075d0b10ddfdb05d',
        'i386': 'b00a3be28bc8200be35666e607f965a0e402091d154631128d07f01b0e376aa4',
    },
    'libswresample4_7%3a5.1.9-0+deb12u1': {
        'amd64': '255406cbb7589751bc5b67efdbc21be01130b9551f5eff2dfe6db2f3e4888a6d',
        'i386': 'fe8ecd9750e456d3324c52e22e0060949ab6f3d3f5c41f39efa51b2d04b7b3fd',
    },
    'libswscale6_7%3a5.1.9-0+deb12u1': {
        'amd64': '9ed5ebab74fc46bfb72f1e4d4cd7769cc98ac67a3320551958f8a507747df450',
        'i386': '04f7514496bf25b63ece29ce11efc1827152131a47cd3c9fe43c65b7937bf5c5',
    },
    'libthai0_0.1.29-1': {
        'amd64': '37cd66bef851ea0e4af807797ba3ad14d43226f7c4954c1d0a19478e11815bae',
        'i386': 'b553e6c69425c0325c7193e3f41241f449c86b865bd0c0e75bee76bae81485e2',
    },
    'libtheora0_1.1.1+dfsg.1-16.1+deb12u1': {
        'amd64': '72739e29f804cbe10a207f546dfec44a340d31bb03f6a503ab6df0cdaf8a148e',
        'i386': '7205448469c6c38d61d6d1d8bb53f2788dab93924627ca735511fda09908d63f',
    },
    'libtwolame0_0.4.0-2': {
        'amd64': '279802de79d682a671cdb11295abb5cd2d21da5bee6ceefa44324c70a29365b1',
        'i386': '454e0fa798aca26d05937cbcccc3546196fd294919d417003646931b7dd0c293',
    },
    'libudfread0_1.1.2-1': {
        'amd64': '93169ca95de3825c5ebda01db79f800fa1c5c4820ac5c760e1234e590d7b4c7b',
        'i386': 'ef72c4fd093a784878fd3a744816323bc1ac030f7181d7fc2ea96552fe031f32',
    },
    'libva-drm2_2.17.0-1': {
        'amd64': 'dc0a0cadbdc001df3d89855d8054aacb332a731f98c7741ce580492b0e01e605',
        'i386': '91a89599931d8896db6f68be1c81436a4bbd5a746d774c543d5505f6a3903d5e',
    },
    'libva-x11-2_2.17.0-1': {
        'amd64': '95db1ecd8c2d1c3f99a750f1b2d9ba1959b7cd9cbd7eabfc1b8a86a095b379a6',
        'i386': '1e55254890ef545b8c571e1b794a56024545bf062f8f2843899889f926df9aec',
    },
    'libva2_2.17.0-1': {
        'amd64': 'e6acf0c4071c01df7c35d18a97768047294dc43d252caae9ccddc1e507456695',
        'i386': '2aa84eea8a1e74354448cf6819e0c04a6c32c2822b3a33d7c6991c16abb55763',
    },
    'libvdpau1_1.5-2': {
        'amd64': '517039c1bfbfc453c4b952a068a1b02b77bf46bbf7af882ad877a581bf2c0e21',
        'i386': '29f8a10b5494139eafca4e686d5919e3e69be807af8455438c40de552b212f66',
    },
    'libvidstab1.1_1.1.0-2+b1': {
        'amd64': '5e0750b6eb5ad8d71e8241c38b0d757e42ee6f9eb6f6dfea64005c5ad4975a39',
        'i386': 'ee937a2690f4cc1d43b28bd89d5aa97596798ea4e2c8f90db363b3bdb32baeb8',
    },
    'libvorbisfile3_1.3.7-1': {
        'amd64': '68f84034ae1c9fa0ab6a45f5b6c406a50faae4421f0d78874260aefa2df68efc',
        'i386': 'f8c55ac04fce9d031321b98aead17ac9a0c8a08c2f1e4fd12fbc9cb3f5e966bf',
    },
    'libvpx7_1.12.0-1+deb12u5': {
        'amd64': '7d8fada67fc9e11a08304499fe5beb7f809194d284202b9c9f1a0b8e54b90db4',
        'i386': 'e57c6d628df993e1fa35e4bc6de397271c9b2d9a6a73626e4262f329fbf7a6fc',
    },
    'libvulkan1_1.3.239.0-1': {  # amd64-only closure
        'amd64': '622f28b807ba55a1f5ad1622ff83aa2c5b11fedbec074850e04c10a8f28b43d3',
    },
    'libwebp7_1.2.4-0.2+deb12u1': {
        'amd64': '7259b7ce46444694ce536360ad53acb68eb3b47a7ff81d7b1b8a3939b2ac9918',
        'i386': 'b596ae0e0f830a514e3b26451b2c3404074ed3cc677799a588c0ed38952db4e8',
    },
    'libwebpmux3_1.2.4-0.2+deb12u1': {
        'amd64': 'cc7d04d56ff8112ef5ed94050b0a440a9b4cea9d1d6f97e2a4016c8cc042c2f1',
        'i386': '4f885f3e8d3d49694b4f67f2eabf30cb352cfdd666da40e98329a253a7a16084',
    },
    'libx264-164_2%3a0.164.3095+gitbaee400-3': {
        'amd64': '2b9f0d79ab1dccc10c24963c1debaf5a37d839bdba369ac358deefd1a7d56a12',
        'i386': '841509b7981f998ebb64f31ecb04be8e2dcdbf8ec3cd9918bda26e8a35f04eb0',
    },
    'libx265-199_3.5-2+b1': {
        'amd64': '9cd87d1b0c56f34f51bcbe8bdb55ebb45dd08ce6c0c6ff2dc77378bac3f64cc0',
        'i386': '9c4e46ac08327620715c63c33acb5cd54bf4a2496673928f46a865b38e5c939d',
    },
    'libxcb-dri3-0_1.15-1': {
        'amd64': '02699b144b9467de8636d27a76984b8f4e7b66e2d25d96df2b9677be86ee9a29',
        'i386': '2142f830d1d7ab016f79e026a13100f6622e113405402a2e02d9dea0ba381e41',
    },
    'libxcb-render0_1.15-1': {
        'amd64': '29c32cc76be9393f409378be79a0f534b9f07dea06ae52fa80a7f27e298de239',
        'i386': '11addf305dc9ac23c78694517b272f6b4dee6d2e9d89497f1da6221fd93e92c6',
    },
    'libxcb-shm0_1.15-1': {
        'amd64': 'c1afcef29dc78b95c475159b181b28b1dedaf1d5aa06efd2fa6d90c73bfbe0e5',
        'i386': 'c9071375b6e4b672cef68cef9dc6b4fe35fcca1424d1a544a027589aa9dc37b4',
    },
    'libxml2_2.9.14+dfsg-1.3~deb12u6': {
        'amd64': '4460e39dda10a815881374217cde08474747cfa018358cd8612c14b390eff53b',
        'i386': '56e525a42ebec9e88ebcbf352c72d98dc0536557dc3459fce97f3e28d8ec89df',
    },
    'libxvidcore4_2%3a1.3.7-1': {
        'amd64': '048e4d456992c7f593564e8ad1cecfc4be0464bfb1fc7a8908a643d59d9fbe81',
        'i386': 'c91ac3ddfd171c4e2027d7c20dcc967ec761fb2678f1cf75cd94d7c59fcea754',
    },
    'libzimg2_3.0.4+ds1-1': {
        'amd64': '61c902185365b5d6a2615f97e7c9baecdfb8e53b6c318396da3022c5b2e8734a',
        'i386': 'e8a0bc8a85e4cc990dff847f16b9d5c7906911c5c450dc4927d2900432e89883',
    },
    'libzmq5_4.3.4-6': {
        'amd64': '45eaea8e29e6ff981943bc88906b1c6e29643b4a781eed9bc139f5af633c97c6',
        'i386': 'ac3310ab5bba43bb6528749db20219392848d368360deceb40cc6c797cda8e61',
    },
    'libzvbi0_0.2.41-1+deb12u1': {
        'amd64': 'b16e63009c643c71136fefa227c77e6d88609a2ac273091b43ce363d1dedf492',
        'i386': '9abac1171a469734e0da534001ec200533a1dc8eecd52c8eed4fddb712a671a4',
    },
}

# The GStreamer plugin set needed for Wine MF/DS cutscene playback. Each plugin
# is a .so under gstreamer-1.0/ whose whole DT_NEEDED closure is staged too.
PLUGINS = (
    'typefindfunctions', 'playback', 'libav', 'avi', 'isomp4', 'matroska',
    'flv', 'ogg', 'audioconvert', 'videoconvertscale', 'audioresample',
    'volume', 'app', 'subparse', 'wavparse', 'flac', 'vorbis', 'audioparsers',
)

ARCHES = {
    'amd64': {'lib_dir': 'usr/lib/x86_64-linux-gnu', 'stage': 'gstreamer64'},
    'i386': {'lib_dir': 'usr/lib/i386-linux-gnu', 'stage': 'gstreamer32'},
}

# Debian source package behind each binary package (pool layout rule:
# pool/main/<first 4 chars if source starts with lib, else first char>/<source>).
SOURCES = {
    'gstreamer1.0-libav': 'gst-libav1.0',
    'gstreamer1.0-plugins-base': 'gst-plugins-base1.0',
    'gstreamer1.0-plugins-good': 'gst-plugins-good1.0',
    'libaom3': 'aom',
    'libass9': 'libass',
    'libavcodec59': 'ffmpeg',
    'libavfilter8': 'ffmpeg',
    'libavformat59': 'ffmpeg',
    'libavutil57': 'ffmpeg',
    'libblas3': 'lapack',
    'libblkid1': 'util-linux',
    'libbluray2': 'libbluray',
    'libbrotli1': 'brotli',
    'libbs2b0': 'libbs2b',
    'libc6': 'glibc',
    'libcairo-gobject2': 'cairo',
    'libcairo2': 'cairo',
    'libchromaprint1': 'chromaprint (1.5.1-2)',
    'libcjson1': 'cjson',
    'libcodec2-1.0': 'codec2',
    'libcom-err2': 'e2fsprogs (1.47.0-2)',
    'libdatrie1': 'libdatrie (0.2.13-2)',
    'libdav1d6': 'dav1d',
    'libdrm2': 'libdrm (2.4.114-1)',
    'libepoxy0': 'libepoxy',
    'libexpat1': 'expat',
    'libflite1': 'flite',
    'libfontconfig1': 'fontconfig',
    'libfribidi0': 'fribidi',
    'libgdk-pixbuf-2.0-0': 'gdk-pixbuf',
    'libgfortran5': 'gcc-12',
    'libglib2.0-0': 'glib2.0',
    'libgme0': 'game-music-emu',
    'libgomp1': 'gcc-12',
    'libgraphite2-3': 'graphite2',
    'libgsm1': 'libgsm',
    'libgssapi-krb5-2': 'krb5',
    'libgstreamer-plugins-base1.0-0': 'gst-plugins-base1.0',
    'libharfbuzz0b': 'harfbuzz',
    'libhwy1': 'highway',
    'libicu72': 'icu',
    'libjpeg62-turbo': 'libjpeg-turbo',
    'libjxl0.7': 'jpeg-xl',
    'libk5crypto3': 'krb5',
    'libkeyutils1': 'keyutils',
    'libkrb5-3': 'krb5',
    'libkrb5support0': 'krb5',
    'liblapack3': 'lapack',
    'liblcms2-2': 'lcms2',
    'liblilv-0-0': 'lilv',
    'libmbedcrypto7': 'mbedtls',
    'libmfx1': 'intel-mediasdk',
    'libmount1': 'util-linux',
    'libmysofa1': 'libmysofa',
    'libnorm1': 'norm',
    'libnuma1': 'numactl',
    'libopenjp2-7': 'openjpeg2',
    'libopenmpt0': 'libopenmpt',
    'libpango-1.0-0': 'pango1.0',
    'libpangocairo-1.0-0': 'pango1.0',
    'libpangoft2-1.0-0': 'pango1.0',
    'libpgm-5.3-0': 'libpgm',
    'libpixman-1-0': 'pixman',
    'libplacebo208': 'libplacebo',
    'libpng16-16': 'libpng1.6',
    'libpocketsphinx3': 'pocketsphinx',
    'libpostproc56': 'ffmpeg',
    'libquadmath0': 'gcc-12',
    'librabbitmq4': 'librabbitmq',
    'librav1e0': 'rust-rav1e',
    'librist4': 'librist',
    'librsvg2-2': 'librsvg',
    'librubberband2': 'rubberband',
    'libselinux1': 'libselinux (3.4-1)',
    'libserd-0-0': 'serd',
    'libshine3': 'shine',
    'libsnappy1v5': 'snappy',
    'libsodium23': 'libsodium',
    'libsord-0-0': 'sord',
    'libsoxr0': 'libsoxr',
    'libspeex1': 'speex',
    'libsphinxbase3': 'sphinxbase',
    'libsratom-0-0': 'sratom',
    'libsrt1.5-gnutls': 'srt',
    'libssh-gcrypt-4': 'libssh',
    'libssl3': 'openssl',
    'libstdc++6': 'gcc-12',
    'libsvtav1enc1': 'svt-av1',
    'libswresample4': 'ffmpeg',
    'libswscale6': 'ffmpeg',
    'libthai0': 'libthai',
    'libtheora0': 'libtheora',
    'libtwolame0': 'twolame',
    'libudfread0': 'libudfread',
    'libva-drm2': 'libva',
    'libva-x11-2': 'libva',
    'libva2': 'libva',
    'libvdpau1': 'libvdpau',
    'libvidstab1.1': 'libvidstab (1.1.0-2)',
    'libvorbisfile3': 'libvorbis',
    'libvpx7': 'libvpx',
    'libvulkan1': 'vulkan-loader',
    'libwebp7': 'libwebp',
    'libwebpmux3': 'libwebp',
    'libx264-164': 'x264',
    'libx265-199': 'x265 (3.5-2)',
    'libxcb-dri3-0': 'libxcb',
    'libxcb-render0': 'libxcb',
    'libxcb-shm0': 'libxcb',
    'libxml2': 'libxml2',
    'libxvidcore4': 'xvidcore',
    'libzimg2': 'zimg',
    'libzmq5': 'zeromq3',
    'libzvbi0': 'zvbi',
}

def pool_urls(pkg: str, arch: str) -> list:
    """Candidate URLs for a package: Debian pool plus the security archive
    (bookworm -updates land there once supersed)."""
    name = pkg.split('_')[0]
    source = SOURCES[name]
    prefix = source[:4] if source.startswith('lib') else source[:1]
    base = f'{{0}}/{prefix}/{source}/{pkg}_{arch}.deb'
    return [
        base.format('http://deb.debian.org/debian/pool/main'),
        base.format('http://security.debian.org/debian-security/pool/updates/main'),
    ]

def sha256(data):
    return hashlib.sha256(data).hexdigest()

def rootfs_libs(arch: str) -> set:
    """Basenames of shared libs already provided by the shipped runtime zips."""
    here = Path(__file__).resolve().parent
    runtime = here.parent.parent / 'web' / 'runtime'
    libs = set()
    for name in (f'glibc-rootfs{64 if arch == "amd64" else 32}.zip',
                 f'wine{64 if arch == "amd64" else 32}.zip'):
        zip_path = runtime / name
        if not zip_path.is_file():
            raise ValueError(f'missing {zip_path} — check out the runtime assets first')
        with zipfile.ZipFile(zip_path) as z:
            libs.update({n.rsplit('/', 1)[-1] for n in z.namelist() if '.so' in n})
    return libs

def download(url, destination):
    request = urllib.request.Request(url, headers={'User-Agent': 'winbox-dependency-stager/1'})
    with urllib.request.urlopen(request, timeout=120) as response, destination.open('wb') as output:
        shutil.copyfileobj(response, output)

def resolve_needed(path):
    out = subprocess.run(['readelf', '-d', str(path)], capture_output=True, text=True).stdout
    return re.findall(r'Shared library: \[(.+?)\]', out)

def stage_arch(arch: str, debs_dir: Path, extracted: Path, stage_root: Path) -> int:
    info = ARCHES[arch]
    if not (extracted / info['lib_dir'] / 'gstreamer-1.0').is_dir():
        raise ValueError(f'{arch}: extracted debs missing gstreamer-1.0/')
    already = rootfs_libs(arch)
    all_libs = {}
    for lib_dir in (info['lib_dir'], info['lib_dir'].split('/', 1)[1]):
        base = extracted / lib_dir
        if not base.is_dir():
            continue
        for path in base.iterdir():
            if '.so' in path.name:
                all_libs[path.name] = path
        for sub in base.iterdir():
            if sub.is_dir():
                for path in sub.iterdir():
                    if path.is_file() and '.so' in path.name:
                        all_libs[path.name] = path
    def resolve(lib):
        if lib not in all_libs:
            raise ValueError(f'{arch}: library not found in staged debs: {lib}')
        return all_libs[lib]

    seen = set()
    def closure(path, seen):
        for lib in resolve_needed(path):
            if lib in seen or lib in already:
                continue
            seen.add(lib)
            closure(resolve(lib), seen)

    entries = {}
    plugin_paths = [extracted / info['lib_dir'] / 'gstreamer-1.0' / f'libgst{p}.so' for p in PLUGINS]
    for plugin in plugin_paths:
        if not plugin.is_file():
            raise ValueError(f'{arch}: missing plugin {plugin.name}')
        closure(plugin, seen)
    for lib in sorted(seen):
        path = all_libs[lib]
        real = path.resolve()
        entries[path.name] = ('link', os.readlink(path)) if path.is_symlink() else ('file', None)
        entries[real.name] = ('file', None)
    for plugin in plugin_paths:
        entries[f'gstreamer-1.0/{plugin.name}'] = ('file', None)

    stage_root.mkdir(parents=True, exist_ok=True)
    src_lib_dir = info['lib_dir'].split('/', 1)[1]
    for rel, (kind, target) in sorted(entries.items()):
        src = extracted / info['lib_dir'] / rel
        if not src.exists() and not src.is_symlink():
            src = extracted / src_lib_dir / rel
        if not src.exists() and not src.is_symlink():
            # alternatives-style subdirs (blas/, lapack/, ...)
            src = next(extracted.glob(f'{info["lib_dir"]}/*/{rel}'), src)
            src = next(extracted.glob(f'{src_lib_dir}/*/{rel}'), src) if not src.exists() and not src.is_symlink() else src
        dst = stage_root / info['lib_dir'] / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if kind == 'link':
            dst.symlink_to(target)
        else:
            shutil.copy2(src, dst)
    return len(entries)


def check_arch(arch: str, stage_root: Path) -> bool:
    info = ARCHES[arch]
    extracted_lib = stage_root / info['lib_dir']
    all_libs = {p.name: p for p in extracted_lib.iterdir() if '.so' in p.name}
    if not all_libs:
        return False
    already = rootfs_libs(arch)
    def resolve(lib):
        if lib not in all_libs:
            raise ValueError(f'{arch}: missing library {lib}')
        return all_libs[lib]

    seen = set()
    errors = []
    for plugin in PLUGINS:
        path = extracted_lib / 'gstreamer-1.0' / f'libgst{plugin}.so'
        if not path.is_file():
            errors.append(f'missing plugin {plugin}.so')
            continue
        try:
            def walk(p, closure_seen):
                for lib in resolve_needed(p):
                    if lib in closure_seen or lib in already:
                        continue
                    closure_seen.add(lib)
                    walk(resolve(lib), closure_seen)
            walk(path, seen)
        except ValueError as error:
            errors.append(str(error))
    if errors:
        raise ValueError('; '.join(errors))
    return True

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--debs', type=Path, help='pre-downloaded .deb cache directory')
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    debs = args.debs or (here / '.gstreamer-debs')
    stage = here
    if not args.check:
        debs.mkdir(parents=True, exist_ok=True)
        for arch in ARCHES:
            for pkg, hashes in DEBS.items():
                if arch not in hashes:
                    continue  # amd64-only package (upstream or closure)
                f = debs / f'{pkg}_{arch}.deb'
                if args.debs and not f.is_file():
                    # --debs given: skip missing packages (partial cache OK)
                    continue
                if not f.is_file() or sha256(f.read_bytes()) != hashes[arch]:
                    tmp = f.with_suffix(f'{f.suffix}.tmp')
                    last_error = None
                    for url in pool_urls(pkg, arch):
                        try:
                            download(url, tmp)
                            last_error = None
                            break
                        except urllib.error.URLError as error:
                            last_error = error
                    if last_error is not None:
                        raise SystemExit(f'failed to download {url}.')
                    if sha256(tmp.read_bytes()) != hashes[arch]:
                        raise SystemExit(f'sha256 mismatch for {url}')
                    tmp.rename(f)
    if args.check:
        for arch in ARCHES:
            info = ARCHES[arch]
            stage_root = stage / info['stage']
            if not stage_root.is_dir():
                raise SystemExit(f'missing {stage_root}: run without --check to stage')
            check_arch(arch, stage_root)
        print('GStreamer plugins: staged and verified.')
        return
    import tempfile
    for arch in ARCHES:
        with tempfile.TemporaryDirectory(prefix='bw64-gst-') as tmp:
            extracted = Path(tmp) / arch
            for pkg, hashes in DEBS.items():
                if arch not in hashes:
                    continue  # amd64-only package
                f = debs / f'{pkg}_{arch}.deb'
                if not f.is_file():
                    raise SystemExit(f'missing deb {f}: rerun download')
                subprocess.run(
                    ['dpkg-deb', '-x', str(f), str(extracted)],
                    check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                )
            info = ARCHES[arch]
            stage_root = stage / info['stage']
            if stage_root.exists():
                shutil.rmtree(stage_root)
            count = stage_arch(arch, debs, extracted, stage_root)
            print(f'{arch}: staged {count} files to {stage_root}')

if __name__ == '__main__':
    main()
