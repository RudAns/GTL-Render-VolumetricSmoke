"""Build the assets the demo needs, headless:

  UnrealEditor-Cmd.exe VolumetricSmoke.uproject -run=pythonscript -script="<abs path>/Tools/build_assets.py"

Creates under /Game/SmokeLab/Materials: M_Voxel (unlit, colour from per-instance custom data), M_Tracer
(additive streak), the surface materials of the drill map when their textures exist (SourceAssets/textures),
and the map /Game/Maps/Range holding one SLLevel actor (the level builds itself at BeginPlay).
"""
import os
import sys
import unreal

ROOT = "/Game/SmokeLab/Materials"
AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
PROJ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def log(*a):
    unreal.log("[build_assets] " + " ".join(str(x) for x in a))


class Exists(Exception):
    pass


def fresh(name, cls, factory, path=ROOT):
    # Existing materials are kept: some are loaded (rooted) by class defaults when the editor starts and cannot be
    # rebuilt in place. Delete the asset to rebuild it.
    full = f"{path}/{name}"
    if EAL.does_asset_exist(full):
        raise Exists(full)
    return AT.create_asset(name, path, cls, factory)


def build(fn, *args, **kw):
    try:
        fn(*args, **kw)
    except Exists as e:
        log("kept", e)


def set_flags(mat, **flags):
    for k, v in flags.items():
        try:
            mat.set_editor_property(k, v)
        except Exception as e:  # noqa
            log("flag", k, e)


class Graph:
    def __init__(self, mat):
        self.mat = mat
        self.y = 0

    def node(self, cls, x=-1200, **props):
        e = MEL.create_material_expression(self.mat, cls, x, self.y)
        self.y += 110
        for k, v in props.items():
            e.set_editor_property(k, v)
        return e

    def custom(self, code, inputs, out_type, desc, x=-400):
        c = self.node(unreal.MaterialExpressionCustom, x)
        c.set_editor_property("code", code)
        c.set_editor_property("description", desc)
        c.set_editor_property("output_type", out_type)
        ins = []
        for name, _ in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", name)
            ins.append(ci)
        c.set_editor_property("inputs", ins)
        for name, (src, out) in inputs:
            MEL.connect_material_expressions(src, out, c, name)
        return c


F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3


def build_voxel():
    mat = fresh("M_Voxel", unreal.Material, unreal.MaterialFactoryNew())
    set_flags(mat, shading_model=unreal.MaterialShadingModel.MSM_UNLIT, used_with_instanced_static_meshes=True)
    g = Graph(mat)
    r = g.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=0)
    gg = g.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=1)
    b = g.node(unreal.MaterialExpressionPerInstanceCustomData, data_index=2)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    emi = g.custom("float2 e = abs(UV - 0.5) * 2.0; float edge = smoothstep(0.82, 0.95, max(e.x, e.y)); return float3(R, G, B) * lerp(1.0, 0.35, edge);",
                   [("R", (r, "")), ("G", (gg, "")), ("B", (b, "")), ("UV", (uv, ""))], F3, "VoxelColour")
    MEL.connect_material_property(emi, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def build_tracer():
    mat = fresh("M_Tracer", unreal.Material, unreal.MaterialFactoryNew())
    set_flags(mat, blend_mode=unreal.BlendMode.BLEND_ADDITIVE, shading_model=unreal.MaterialShadingModel.MSM_UNLIT, two_sided=True)
    g = Graph(mat)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    emi = g.custom("float t = UV.y; return float3(1.0, 0.72, 0.32) * 18.0 * smoothstep(0.0, 0.35, t) * (1.0 - smoothstep(0.85, 1.0, t));",
                   [("UV", (uv, ""))], F3, "TracerGlow")
    MEL.connect_material_property(emi, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)


def build_map():
    path = "/Game/Maps/Range"
    if EAL.does_asset_exist(path):
        log("map exists")
        return
    world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    lvl = sub.spawn_actor_from_class(unreal.SLLevel, unreal.Vector(0, 0, 0))
    log("level actor", lvl)
    log("map saved", unreal.EditorLoadingAndSavingUtils.save_map(world, path))


def main():
    build(build_voxel)
    build(build_tracer)
    try:
        import build_surfaces  # optional: PBR surface materials from SourceAssets/textures
        build_surfaces.run(fresh, Graph, log, build)
    except ImportError:
        log("no surface textures yet (prototype materials are used)")
    build_map()
    EAL.save_directory("/Game/SmokeLab", only_if_is_dirty=False, recursive=True)
    log("done")


main()
