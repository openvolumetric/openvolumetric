import nuke

from openvolumetric_read import create_openvolumetric_read


openvolumetric_menu = nuke.menu("Nodes").addMenu(
    "OpenVolumetric", icon="OpenVolumetric.png"
)
openvolumetric_menu.addCommand(
    "OpenVolumetricRead",
    create_openvolumetric_read,
)
