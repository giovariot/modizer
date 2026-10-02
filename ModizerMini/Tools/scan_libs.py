#!/usr/bin/env python3
"""Extract, from each subproject in libs/, the list of compiled sources and the
relevant build settings, so they can be recompiled for macOS."""
import os, plistlib, subprocess, sys, json

ROOT = "/Users/giovanni/Documents/Sviluppo/conductor/workspaces/modizer/belgrade"

PROJECTS = [
    "libs/Adplug/adplug.xcodeproj",
    "libs/binio/binio.xcodeproj",
    "libs/asap",
    "libs/STSound",
    "libs/AHX",
    "libs/sc68",
    "libs/mdxplay",
    "libs/libatariaudio/libatariaudio.xcodeproj",
    "libs/libeupmini/libeupmini.xcodeproj",
    "libs/libfmpmini/libfmpmini.xcodeproj",
    "libs/libpmdmini/libpmdmini.xcodeproj",
    "libs/libpt3/libpt3/libpt3.xcodeproj",
    "libs/libpixel/libpixel.xcodeproj",
    "libs/libwonderswan/libwonderswan.xcodeproj",
    "libs/libkss/libkss.xcodeproj",
    "libs/libnez/nez/nez.xcodeproj",
    "libs/libgsf/libgsf/gsf.xcodeproj",
    "libs/libnsfplay/libnsfplay.xcodeproj",
    "libs/libfurnace/libfurnace.xcodeproj",
    "libs/libvgm/libvgm.xcodeproj",
    "libs/libvgmstream/vgmstream/vgmstream.xcodeproj",
    "libs/v2mplayer/v2mplayer.xcodeproj",
    "libs/gbsplay/gbsplay.xcodeproj",
    "libs/libopenmpt/libopenmpt-modizer/libopenmpt-modizer.xcodeproj",
    "libs/libsidplayfp/libsidplayfp.xcodeproj",
    "libs/q_placeholder",
    "libs/highlyexperimental/highlyexperimental.xcodeproj",
    "libs/highlytheoritical/highlytheoritical.xcodeproj",
    "libs/HighlyQuixotic/HighlyQuixotic.xcodeproj",
    "libs/libxsf/libxsf/libxsf.xcodeproj",
    "libs/vio2sf/vio2sf.xcodeproj",
    "libs/snsf/snsf.xcodeproj",
    "libs/libLazyusf/LibLazyusf old/Lazyusf.xcodeproj",
    "libs/libpsflib/psflib/psflib.xcodeproj",
    "libs/libtim/tim.xcodeproj",
    "libs/projectm-ios/projectm-ios.xcodeproj",
    "libs/websid/websid.xcodeproj",
    "libs/uade-2.13/uade/uade.xcodeproj",
    "libs/monkeyaudiocodec/monkeyaudiocodec.xcodeproj",
    "libs/libchpconv/libchpconv.xcodeproj",
    "libs/libg719/libg719.xcodeproj",
    "libs/libzxtune/libzxtune.xcodeproj",
]


def load(projpath):
    pbx = os.path.join(ROOT, projpath, "project.pbxproj")
    out = subprocess.check_output(["plutil", "-convert", "xml1", "-o", "-", pbx])
    return plistlib.loads(out)


def resolve_file_path(obj, objects, projdir):
    """Resolve a PBXFileReference to a path relative to the repo root."""
    # Path is relative to the group chain; simplest reliable approach: use the
    # file's name/path plus the parent groups' paths.
    return None


def walk_groups(objects):
    """Return dict: fileRefID -> full relative path (relative to project dir)."""
    result = {}
    root = None
    for oid, o in objects.items():
        if o.get("isa") == "PBXProject":
            root = o.get("mainGroup")
            break
    if root is None:
        return result

    def visit(group_id, prefix):
        g = objects.get(group_id)
        if not g:
            return
        for child in g.get("children", []):
            c = objects.get(child)
            if not c:
                continue
            isa = c.get("isa")
            if isa in ("PBXGroup", "PBXFileSystemSynchronizedRootGroup", "PBXVariantGroup"):
                name = c.get("path") or c.get("name") or ""
                visit(child, os.path.join(prefix, name) if name else prefix)
            elif isa == "PBXFileReference":
                name = c.get("path") or c.get("name") or ""
                result[child] = os.path.join(prefix, name) if name else c.get("name", "")

    visit(root, "")
    return result


def target_info(objects, projdir):
    idmap = walk_groups(objects)
    out = []
    for oid, o in objects.items():
        if o.get("isa") != "PBXNativeTarget":
            continue
        name = o.get("name")
        sources = []
        for phase_id in o.get("buildPhases", []):
            phase = objects.get(phase_id)
            if not phase or phase.get("isa") != "PBXSourcesBuildPhase":
                continue
            for bf_id in phase.get("files", []):
                bf = objects.get(bf_id)
                if not bf:
                    continue
                ref = bf.get("fileRef")
                p = idmap.get(ref)
                if p:
                    sources.append(os.path.normpath(os.path.join(projdir, p)))
        # settings from all configs
        settings = {}
        cl = o.get("buildConfigurationList")
        if cl:
            for cid in objects.get(cl, {}).get("buildConfigurations", []):
                c = objects.get(cid)
                if not c:
                    continue
                for k, v in c.get("buildSettings", {}).items():
                    if k in ("GCC_PREPROCESSOR_DEFINITIONS", "HEADER_SEARCH_PATHS",
                             "OTHER_CFLAGS", "OTHER_CPLUSPLUSFLAGS", "GCC_PREFIX_HEADER",
                             "SDKROOT", "IPHONEOS_DEPLOYMENT_TARGET"):
                        settings.setdefault(k, v)
        out.append({"name": name, "settings": settings, "sources": sources})
    return out


def main():
    report = {}
    for proj in PROJECTS:
        full = os.path.join(ROOT, proj)
        if not os.path.exists(full):
            report[proj] = {"error": "missing"}
            continue
        if not proj.endswith(".xcodeproj"):
            report[proj] = {"note": "no xcodeproj", "files": sorted(
                os.path.relpath(os.path.join(dp, f), ROOT)
                for dp, _, fs in os.walk(full) for f in fs
                if f.endswith((".c", ".cpp", ".cc", ".m", ".mm", ".h", ".hpp")))[:400]}
            continue
        try:
            objects = load(proj)["objects"]
        except Exception as e:
            report[proj] = {"error": str(e)}
            continue
        projdir = proj
        report[proj] = target_info(objects, projdir)
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
