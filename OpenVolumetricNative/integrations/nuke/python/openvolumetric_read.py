"""Artist-facing OpenVolumetric reader construction for Foundry Nuke."""

import math
import nuke


_TIMING_KNOBS = (
    "start_frame",
    "frame_offset",
    "source_frame_rate",
    "trim_out",
    "before_mode",
    "after_mode",
)
_TEXTURE_KNOBS = (
    "luminance_correction",
    "red_projection_correction",
    "blue_projection_correction",
)
_CACHE_KNOBS = ("cache_frames", "cache_megabytes", "reload_version")
_REFRESHING = set()


def _hidden_knob(knob):
    """Hide an implementation knob while retaining it in saved scripts."""
    knob.setVisible(False)
    return knob


def _add_controls(group):
    """Add the single public control surface shared by the native readers."""
    group.addKnob(nuke.Tab_Knob("openvolumetric", "OpenVolumetric"))
    group.addKnob(nuke.File_Knob("file", "OpenVolumetric MP4"))

    group.addKnob(nuke.Text_Knob("timing_divider", "", "Timing"))
    source_range = nuke.Text_Knob("source_range", "source range", "Select an MP4")
    group.addKnob(source_range)

    trim_in = nuke.Int_Knob("trim_in", "trim in")
    trim_in.setRange(0, 1)
    trim_in.setValue(0)
    trim_in.setTooltip("First inclusive source frame used by this reader.")
    group.addKnob(trim_in)
    trim_out_control = nuke.Int_Knob("trim_out_control", "trim out")
    trim_out_control.setRange(0, 1)
    trim_out_control.setValue(1)
    trim_out_control.setTooltip("Final inclusive source frame used by this reader.")
    group.addKnob(trim_out_control)

    timeline_start = nuke.Int_Knob("timeline_start", "timeline start")
    timeline_start.setValue(1)
    timeline_start.setTooltip("Nuke frame at which the trimmed source begins.")
    group.addKnob(timeline_start)

    before_mode = nuke.Enumeration_Knob("before", "before", ["none", "hold"])
    before_mode.setValue("hold")
    group.addKnob(before_mode)
    after_mode = nuke.Enumeration_Knob("after", "after", ["none", "hold"])
    after_mode.setValue("hold")
    group.addKnob(after_mode)

    start_frame = _hidden_knob(nuke.Int_Knob("start_frame", "start frame"))
    start_frame.setValue(1)
    group.addKnob(start_frame)
    group.addKnob(_hidden_knob(nuke.Double_Knob("frame_offset", "frame offset")))
    group.addKnob(_hidden_knob(nuke.Double_Knob("source_frame_rate", "source frame rate")))
    group.addKnob(_hidden_knob(nuke.Double_Knob("trim_out", "trim out")))
    group.addKnob(_hidden_knob(nuke.Int_Knob("before_mode", "before mode")))
    group.addKnob(_hidden_knob(nuke.Int_Knob("after_mode", "after mode")))
    group.addKnob(_hidden_knob(nuke.String_Knob("metadata_source", "metadata source")))

    group.addKnob(nuke.Text_Knob("texture_divider", "", "Texture"))
    group.addKnob(
        nuke.Double_Knob("luminance_correction", "luminance correction")
    )
    group.addKnob(
        nuke.Double_Knob(
            "red_projection_correction", "red projection correction"
        )
    )
    group.addKnob(
        nuke.Double_Knob(
            "blue_projection_correction", "blue projection correction"
        )
    )

    group.addKnob(nuke.Text_Knob("cache_divider", "", "Cache"))
    cache_frames = nuke.Int_Knob("cache_frames", "cached frames")
    cache_frames.setValue(8)
    cache_frames.setTooltip("Maximum complete presentations retained per source.")
    group.addKnob(cache_frames)
    cache_megabytes = nuke.Int_Knob("cache_megabytes", "cache memory (MB)")
    cache_megabytes.setValue(256)
    cache_megabytes.setTooltip("Maximum estimated decoded payload retained per source.")
    group.addKnob(cache_megabytes)

    reload_version = nuke.Int_Knob("reload_version", "reload version")
    reload_version.setVisible(False)
    group.addKnob(reload_version)
    reload_button = nuke.PyScript_Knob(
        "reload_source",
        "reload source",
        "import openvolumetric_read; "
        "openvolumetric_read.reload_reader(nuke.thisNode())",
    )
    reload_button.setTooltip(
        "Discard this reader's current decoder session and reopen the MP4."
    )
    group.addKnob(reload_button)

    status = nuke.Text_Knob(
        "reader_status",
        "input mode",
        "Local MP4 / embedded representation",
    )
    status.setTooltip(
        "Manifest and fixed remote representation selection are planned for Phase 8."
    )
    group.addKnob(status)

    # Nuke executes this on the UI thread when an artist edits a group knob.
    # It keeps both native children on one derived timing contract.
    group["knobChanged"].setValue(
        "import openvolumetric_read; openvolumetric_read.knob_changed()"
    )


def _internal_node(group, name):
    """Return a named child without leaking Group begin/end state."""
    group.begin()
    try:
        return nuke.toNode(name)
    finally:
        group.end()


def _synchronize_timing(group, changed=""):
    """Translate artist timing controls into the native frame mapping."""
    trim_in = float(group["trim_in"].value())
    trim_out = float(group["trim_out_control"].value())
    if trim_in > trim_out:
        # Keep the interval valid while allowing either endpoint to be edited.
        if changed == "trim_in":
            trim_out = trim_in
            group["trim_out_control"].setValue(int(trim_out))
        else:
            trim_in = trim_out
            group["trim_in"].setValue(int(trim_in))
    group["start_frame"].setValue(int(group["timeline_start"].value()))
    group["frame_offset"].setValue(trim_in)
    group["trim_out"].setValue(trim_out)
    group["before_mode"].setValue(1 if group["before"].value() == "hold" else 0)
    group["after_mode"].setValue(1 if group["after"].value() == "hold" else 0)


def _refresh_metadata(group, reset_trim=False):
    """Read metadata already opened by the internal texture reader."""
    source = group["file"].evaluate()
    if not source:
        group["source_range"].setValue("Select an MP4")
        return

    texture = _internal_node(group, "OpenVolumetricTextureInternal")
    if texture is None:
        return
    texture.forceValidate()
    frame_rate = float(texture["media_frame_rate"].value())
    duration = float(texture["media_duration"].value())
    if frame_rate <= 0.0 or duration <= 0.0:
        group["source_range"].setValue("Unable to read source metadata")
        return

    final_frame = max(0, int(math.ceil(duration * frame_rate)) - 1)
    trim_in = group["trim_in"]
    trim_out = group["trim_out_control"]
    trim_in.setRange(0, final_frame)
    trim_out.setRange(0, final_frame)
    source_changed = group["metadata_source"].value() != source
    if reset_trim or source_changed:
        trim_in.setValue(0)
        trim_out.setValue(final_frame)
    else:
        trim_in.setValue(max(0, min(int(trim_in.value()), final_frame)))
        trim_out.setValue(max(0, min(int(trim_out.value()), final_frame)))
    group["metadata_source"].setValue(source)
    group["source_range"].setValue(
        "0-%d  |  %.3f fps  |  %.3f s" % (final_frame, frame_rate, duration)
    )
    _synchronize_timing(group)


def knob_changed():
    """Handle metadata and timing changes for an OpenVolumetricRead group."""
    group = nuke.thisNode()
    changed = nuke.thisKnob().name()
    identity = group.fullName()
    if identity in _REFRESHING:
        return
    if changed not in (
        "file", "trim_in", "trim_out_control", "timeline_start", "before", "after"
    ):
        return
    _REFRESHING.add(identity)
    try:
        if changed == "file":
            _refresh_metadata(group, reset_trim=True)
        else:
            _synchronize_timing(group, changed)
    finally:
        _REFRESHING.discard(identity)


def _link_string(child, knob_name):
    """Resolve a group string knob dynamically inside an internal node."""
    child[knob_name].setValue("[value parent.%s]" % knob_name)


def _link_numeric(child, knob_name):
    """Drive an internal numeric knob from its identically named group knob."""
    child[knob_name].setExpression("parent.%s" % knob_name)


def _add_transform_controls(group):
    """Expose the internal SourceGeomOp transform without duplicating values."""
    group.addKnob(nuke.Tab_Knob("transform_controls", "Transform"))
    controls = (
        ("xform_order", "transform order"),
        ("rot_order", "rotation order"),
        ("translate", "translate"),
        ("rotate", "rotate"),
        ("scaling", "scale"),
        ("uniform_scale", "uniform scale"),
        ("skew", "skew"),
        ("pivot", "pivot"),
    )
    for knob_name, label in controls:
        link = nuke.Link_Knob(knob_name, label)
        # A relative link remains valid when the Group is cloned, renamed, or
        # restored into a different parent Group.
        link.setLink("OpenVolumetricGeoInternal.%s" % knob_name)
        group.addKnob(link)


def reload_reader(group):
    """Select a fresh shared native session and invalidate both internal nodes."""
    version = group["reload_version"]
    version.setValue(version.value() + 1)
    _refresh_metadata(group, reset_trim=False)
    group.forceValidate()
    nuke.tprint("OpenVolumetricRead: source and presentation cache reloaded.")


def create_openvolumetric_read():
    """Create one reader whose internal image and geometry share one MP4."""
    group = None
    try:
        group = nuke.nodes.Group(inputs=[], name="OpenVolumetricRead")
        _add_controls(group)

        group.begin()
        try:
            texture = nuke.createNode("OpenVolumetricTexture", inpanel=False)
            texture.setName("OpenVolumetricTextureInternal")
            _link_string(texture, "file")
            for knob_name in _TIMING_KNOBS + _TEXTURE_KNOBS:
                _link_numeric(texture, knob_name)
            for knob_name in _CACHE_KNOBS:
                _link_numeric(texture, knob_name)

            geometry = nuke.createNode("OpenVolumetricGeo", inpanel=False)
            geometry.setName("OpenVolumetricGeoInternal")
            _link_string(geometry, "file")
            # GeoScene composes primitives by scene path. Each Group contains
            # an identically named native geometry node, so SourceGeomOp's
            # default {nodename} path would make multiple readers overwrite
            # one another. The parent Group name is unique and is preserved as
            # a dynamic expression across clone, rename, and script reload.
            geometry["prim_path"].setValue(
                "/[value parent.name]/OpenVolumetricMesh"
            )
            for knob_name in _TIMING_KNOBS:
                _link_numeric(geometry, knob_name)
            for knob_name in _CACHE_KNOBS:
                _link_numeric(geometry, knob_name)

            # SourceGeomOp's material input accepts the synchronized image
            # directly, matching the manually validated node graph.
            geometry.setInput(1, texture)
            output = nuke.nodes.Output()
            output.setInput(0, geometry)
        finally:
            group.end()

        _add_transform_controls(group)
        _synchronize_timing(group)
        group["label"].setValue("[file tail [value file]]")
        group["tile_color"].setValue(0x2A9D8FFF)
        group.showControlPanel()
        return group
    except Exception as error:
        # Do not leave an unusable, partially constructed Group in the script.
        if group is not None:
            nuke.delete(group)
        message = "Unable to create OpenVolumetricRead:\n%s" % error
        nuke.tprint(message)
        nuke.message(message)
        return None
