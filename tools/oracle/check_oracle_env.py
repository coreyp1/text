#!/usr/bin/env python3
"""Fail if the oracle pin table or the code that reads it has rotted.

`tools/oracle/containers/IMAGES` is machine-read, not documentation. What reads
it is `oracle_env.py`, and both can go wrong in ways no differential would
report, because a differential that cannot reach its reference declines rather
than lying - so the failure is silent in the other direction: a pin that parses
into the wrong fields, a reference with no way to be asked its version, a
filter that has stopped filtering.

This needs no container engine, no reference and no build, which is why it is in
`make test` while every gate that consults an oracle is not. It takes
milliseconds.

**Every check here is paired with a control** - a planted violation the check
must catch - because a checker whose assertion no longer reaches its subject
reports the same clean line as one that passes. The controls that matter most
are the last two: they are the only thing standing between "this library raised
its UCD pin" and "its IDNA oracle quietly went back to being two releases
behind", which is the state the whole `containers/` directory exists to leave.

Copyright 2026 by Corey Pennycuff
"""

import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import oracle_env

FAILURES = []
# The name a built-here image must carry, so that one can be told from a stock
# one and `make oracle-clean` can find it.
BUILT_HERE_PREFIX = "ghoti-text-oracle-"
UCD_VERSION_FILE = os.path.join(oracle_env.ROOT, "tools", "idna",
                                "UCD_VERSION")


def fail(message):
    FAILURES.append(message)


def images_saying(text):
    """`oracle_env.pins()` as it would read a planted IMAGES file.

    The control mechanism for every check that takes a table: write the table,
    point the module at it, and clear the cache. Restoring all three afterwards
    is the caller's job, which is why each control does it in a `finally`.
    """
    handle = tempfile.NamedTemporaryFile("w", suffix=".IMAGES", delete=False,
                                         encoding="utf-8")
    handle.write(text)
    handle.close()
    oracle_env.IMAGES = handle.name
    oracle_env._pins = None
    return handle.name


def restore_images(saved, planted):
    oracle_env.IMAGES = saved
    oracle_env._pins = None
    os.unlink(planted)


def audit(table):
    """The rules a pin table has to satisfy. Returns a list of complaints."""
    problems = []
    if not table:
        problems.append("containers/IMAGES names no references at all")
    for name, (image, version, _) in table.items():
        if not image or not version:
            problems.append("%s: an empty image or version field" % name)
        # A stock image is pinned by digest and the digest is the whole
        # guarantee; a built-here one cannot be, and carries the convention's
        # prefix. An image that is neither is one nothing pins.
        if image.startswith("localhost/"):
            if BUILT_HERE_PREFIX not in image:
                problems.append(
                    "%s: built here and outside the naming convention: %s"
                    % (name, image))
        elif "@sha256:" not in image:
            problems.append("%s: a stock image with no digest: %s"
                            % (name, image))
    return problems


def check_images_parses():
    """The committed table parses and satisfies every rule."""
    try:
        table = oracle_env.pins()
    except oracle_env.OracleUnavailable as why:
        fail("containers/IMAGES does not parse: %s" % why)
        return {}
    for problem in audit(table):
        fail(problem)
    return table


def check_images_rules_fire():
    """The control: each rule catches its own violation.

    Without this, `audit()` returning nothing is two different facts - the
    table is right, or the rules stopped reaching it - printing one line.
    """
    saved = oracle_env.IMAGES
    cases = [
        ("a stock image with no digest",
         "python\tdocker.io/library/python:3.14-slim\tunicodedata 16.0.0\tx\n"),
        ("built here and outside the naming convention",
         "idna\tlocalhost/some-other-image:1\tidnadata 17.0.0\tx\n"),
        ("an empty image or version field",
         "python\tdocker.io/library/python@sha256:abc\t\tx\n"),
        ("names no references at all",
         "# nothing but a comment\n"),
    ]
    for expect, text in cases:
        planted = images_saying(text)
        try:
            problems = " ".join(audit(oracle_env.pins()))
        except oracle_env.OracleUnavailable as why:
            problems = str(why)
        finally:
            restore_images(saved, planted)
        if expect not in problems:
            fail("control: a table that is %r was not caught (got %r)"
                 % (expect, problems))
    # And a two-field line must not parse at all, rather than parsing into
    # something with an empty version.
    planted = images_saying("python\tdocker.io/library/python@sha256:abc\n")
    try:
        oracle_env.pins()
    except oracle_env.OracleUnavailable:
        pass
    else:
        fail("control: a two-field IMAGES line was accepted")
    finally:
        restore_images(saved, planted)


def check_every_pin_can_be_asked(table):
    """A pin with no probe reports the empty string as its version.

    That is how `regex`'s copy of this started: a `-next` pin had no PROBE
    entry, so `version()` ran `<name> --version` inside the image, got nothing,
    and returned `''`. It failed closed only because `check_pin` then compared
    the pin against the empty string - the next pin whose version field happened
    to be empty would have passed.
    """
    for name in table:
        if oracle_env._probe(name) is None:
            fail("%s: no PROBE entry and no base name that has one" % name)


def check_probe_fallback():
    """`python-next` probes as `python`, and `nosuch-next` still has no probe."""
    if oracle_env._probe("python-next") is not oracle_env.PROBE["python"]:
        fail("_probe: a `-next` pin does not fall back to its base name")
    if oracle_env._probe("nosuch-next") is not None:
        fail("_probe: a name with no base in PROBE resolved to something")


def check_reference_stderr():
    """Both halves fire, and neither eats what it is not for."""
    noisy = ("Emulate Docker CLI using podman. Create /etc/containers/"
             "nodocker to quiet msg.\n"
             "idna 3.19, idnadata 17.0.0\n"
             "  File \"%s/tools/oracle/idna_ask.py\", line 41"
             % oracle_env.ROOT)
    got = oracle_env.reference_stderr(noisy)
    if "Emulate Docker CLI" in got:
        fail("reference_stderr: the engine's banner survived")
    if oracle_env.ROOT in got:
        fail("reference_stderr: an absolute path survived")
    if "idna 3.19, idnadata 17.0.0" not in got:
        fail("reference_stderr: it ate the reference's own output")
    if "tools/oracle/idna_ask.py" not in got:
        fail("reference_stderr: the relative path is not what was left")
    # The control for the control: a string with neither must come back whole.
    plain = "idna 3.19\nTraceback (most recent call last):"
    if oracle_env.reference_stderr(plain) != plain:
        fail("reference_stderr: it changed text that has nothing to remove")


def check_mode_is_closed():
    """An unknown GHOTI_ORACLE_MODE raises rather than picking one."""
    saved = oracle_env.MODE
    try:
        oracle_env.MODE = "whatever"
        try:
            oracle_env.ensure("python")
        except oracle_env.OracleUnavailable:
            pass
        else:
            fail("ensure: an unknown mode was accepted")
    finally:
        oracle_env.MODE = saved


def pinned_ucd():
    with open(UCD_VERSION_FILE, encoding="utf-8") as handle:
        return handle.read().strip()


def matched_pins(table, ucd):
    """The pins whose version field must name this library's own UCD version.

    Two of the three, and for different reasons. `idna`'s is the one that
    decides how much of the codespace the IDNA gate compares at all - a
    reference behind the pin cannot answer for what it does not assign, and the
    blind spot that opens is where a wrong DISALLOWED of ours passes. `python-
    next`'s is what makes `check-nfc-oracle-strict` mean what its name says: at
    a matching UCD a disagreement is necessarily a defect, and at any other
    version it is a finding to be read.

    The gating `python` pin is deliberately not in this list. It is a released
    interpreter and is *expected* to lag; the gate prints the gap.
    """
    problems = []
    for name in ("idna", "python-next"):
        if name not in table:
            problems.append("%s: no pin at all, and this library's %s gate "
                            "needs one at UCD %s" % (name, name, ucd))
            continue
        said = table[name][1]
        if ucd not in said:
            problems.append(
                "%s: IMAGES says %r, which does not name UCD %s from "
                "tools/idna/UCD_VERSION. Raising the UCD pin without raising "
                "this one puts the oracle behind the tables it checks."
                % (name, said, ucd))
    return problems


def check_matched_pins(table):
    """The committed table's matched pins agree with UCD_VERSION."""
    for problem in matched_pins(table, pinned_ucd()):
        fail(problem)


def check_matched_pins_fire():
    """The control: a stale matched pin, and a missing one, are both caught."""
    ucd = pinned_ucd()
    stale = {"idna": ("localhost/ghoti-text-oracle-idna:3.10",
                      "idnadata 15.1.0", ""),
             "python-next": ("docker.io/library/python@sha256:abc",
                             "unicodedata %s" % ucd, "")}
    problems = " ".join(matched_pins(stale, ucd))
    if "does not name UCD %s" % ucd not in problems:
        fail("control: a stale idna pin was not caught (got %r)" % problems)
    if "no pin at all" in problems:
        fail("control: a present pin was reported missing")
    problems = " ".join(matched_pins({}, ucd))
    if problems.count("no pin at all") != 2:
        fail("control: a missing matched pin was not caught (got %r)"
             % problems)


def main():
    table = check_images_parses()
    check_images_rules_fire()
    check_every_pin_can_be_asked(table)
    check_probe_fallback()
    check_reference_stderr()
    check_mode_is_closed()
    check_matched_pins(table)
    check_matched_pins_fire()
    if FAILURES:
        sys.stderr.write("\033[0;31m\n### The oracle pin table is wrong ###"
                         "\033[0m\n")
        for message in FAILURES:
            sys.stderr.write("  %s\n" % message)
        return 1
    print("oracle pins: %d references, all askable; IMAGES parses; the stderr "
          "filter fires both ways; idna and python-next name UCD %s."
          % (len(table), pinned_ucd()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
