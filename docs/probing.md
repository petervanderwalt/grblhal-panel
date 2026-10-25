# Probing

Jog > Probe opens with Single surface and Z- Top surface selected. Put the
plate on the stock and enter its thickness and the probe feed. The quick contact
check arms when the window opens: lift the plate, touch it to the stationary
tool, then release. Probe becomes enabled after contact and release feedback.
If the 15-second check expires, Test probe starts it again. Confirming Probe
starts the acknowledged sequence; opening the window or choosing a surface
never moves the machine. The verified contact is consumed by each probe run.

Z probes downward up to 25 mm, sets only the active WCS Z offset using plate
thickness, then retracts 2 mm. Tool diameter is not required for Z.
X-/X+ and Y-/Y+ probe the selected direction up to 25 mm. They set only that
axis using minus direction times (plate offset plus tool radius), then retract
2 mm opposite the probing direction. Plate X/Y offsets come from Settings > Probe.
The UI shows converted distances in inch mode; generated commands always use mm.
Original G90/G91 distance mode is restored, followed by $G/$# readback.

Corner mode retains the existing XYZ plate-hole routine and corner selection.
The same quick contact gate applies to both modes. A pending G38.2 requires its
acknowledgement and successful PRB report before offsets or retract commands
are sent. Controller alarms and errors use the existing handling. No extra probe
protection wizard is added.

Implementation follows Ooznest Control js/machine/probe.js runSingleAxis for
relative motion and X/Y compensation; Z uses plate thickness without radius,
as in its Z portion of runAutoCorner. UI is panel_probe_ui.inc; protocol logic
is Model::probe_surface in panel_model.cpp. ProbeSettings/NVS blob is unchanged.
Surface selection is a session UI choice and returns to Z at boot.

Portable model tests cover all five directions, exact offsets, mm output,
distance-mode restoration and acknowledgement/PRB ordering. The physical UI
check is tools/check-probe-surfaces.py, with UART-off demo feedback only.
Physical probing against a live controller and touch plate remains to be tested.
