# unity-volumetric-video

This project enables the playback of volumetric video in unity game engine.
This is implemented as a native C++ plugin


### Encoding Content

#### Geometry
Geometry is encoded using google draco. The following command can be used to encode a single mesh 
``
    draco_encoder -i input.obj -o encoded.drc 
``

#### Texture
Textures are enocded using ffmpeg h265. The following command can be used to encode Textures
``
ffmpeg -i %06d.png  -s 1024x1024 -c:v libx265 -crf 25  -pix_fmt yuv420p -x265-params keyint=20:min-keyint=1:bframes=0:slices=6 -r $FRAMERATE output.mmp4
``


### Contributors:

- Marco Volino - m.volino@surrey.ac.uk



### Limitations

- **Mesh Size**: Meshes can consist of up to 65,535 verticies and 100,000 triangles. This can be changed in the __VolumetricVideoDecoder.cs__ file.


