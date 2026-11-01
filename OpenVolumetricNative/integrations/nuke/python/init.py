"""Relocatable bootstrap for the OpenVolumetric Nuke package."""

import os

import nuke


# Keep image resources separate from executable plug-ins and Python modules.
# The path is resolved relative to this file so the package can be moved or
# placed anywhere on NUKE_PATH without machine-specific configuration.
nuke.pluginAddPath(os.path.join(os.path.dirname(__file__), "icons"))

