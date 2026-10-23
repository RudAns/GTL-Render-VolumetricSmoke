"""Surface materials and props for the drill map (imported by build_assets.py).

Textures and models are Poly Haven CC0 files under SourceAssets/. Textures go through the legacy
TextureFactory so the .uasset files do not record absolute source paths.
"""
import os
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary
SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "SourceAssets")
MAT = "/Game/SmokeLab/Materials"
TEX = "/Game/SmokeLab/Textures"
F1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
F3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3


def import_texture(path, kind, log):
    name = os.path.splitext(os.path.basename(path))[0]
    full = f"{TEX}/{name}"
    if not EAL.does_asset_exist(full):
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", path)
        t.set_editor_property("destination_path", TEX)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", False)
        t.set_editor_property("factory", unreal.TextureFactory())
        AT.import_asset_tasks([t])
    tex = unreal.load_asset(full)
    if tex is None:
        log("texture import failed", path)
        return None
    if kind != "diff":
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS if kind == "arm" else unreal.TextureCompressionSettings.TC_NORMALMAP)
    tex.set_editor_property("max_texture_size", 1024)
    EAL.save_loaded_asset(tex)
    return tex


# Triplanar in world space so the code-built boxes need no UVs (whiteout blend for normals).
TRI = r"""
float3 n = normalize(Nw);
float3 w = pow(abs(n), 4.0); w /= (w.x + w.y + w.z);
float2 ux = WP.zy / Tile, uy = WP.xz / Tile, uz = WP.xy / Tile;
"""
COL = TRI + r"""
float3 c = Diff.Sample(DiffSampler, ux).rgb * w.x + Diff.Sample(DiffSampler, uy).rgb * w.y + Diff.Sample(DiffSampler, uz).rgb * w.z;
// low-frequency grime so large walls do not read as a repeated tile
float g = 0.86 + 0.14 * sin(WP.x * 0.0021 + 1.3) * sin(WP.y * 0.0017 + 0.4) * sin(WP.z * 0.006 + 2.0);
return c * Tint * g;
"""
ARM = TRI + r"""
float3 a = Arm.Sample(ArmSampler, ux).rgb * w.x + Arm.Sample(ArmSampler, uy).rgb * w.y + Arm.Sample(ArmSampler, uz).rgb * w.z;
return float3(a.r, saturate(a.g * RoughMul), a.b * Metal);
"""
NRM = TRI + r"""
float3 tx = Nor.Sample(NorSampler, ux).xyz * 2.0 - 1.0;
float3 ty = Nor.Sample(NorSampler, uy).xyz * 2.0 - 1.0;
float3 tz = Nor.Sample(NorSampler, uz).xyz * 2.0 - 1.0;
tx = float3(tx.xy + n.zy, abs(tx.z) * n.x);
ty = float3(ty.xy + n.xz, abs(ty.z) * n.y);
tz = float3(tz.xy + n.xy, abs(tz.z) * n.z);
return normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z);
"""


def triplanar(fresh, Graph, log, name, texname, tile, tint=(1, 1, 1), rough=1.0, metal=0.0):
    d = os.path.join(SRC, "textures", texname)
    files = os.listdir(d)
    pick = lambda k: os.path.join(d, next(f for f in files if f"_{k}_" in f))
    diff = import_texture(pick("diff"), "diff", log)
    nor = import_texture(pick("nor_gl"), "nor", log)
    arm = import_texture(pick("arm"), "arm", log)
    mat = fresh(name, unreal.Material, unreal.MaterialFactoryNew(), MAT)
    mat.set_editor_property("tangent_space_normal", False)
    g = Graph(mat)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    nw = g.node(unreal.MaterialExpressionVertexNormalWS)
    td = g.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="Diff", texture=diff)
    tn = g.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="Nor", texture=nor)
    ta = g.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="Arm", texture=arm)
    ti = g.node(unreal.MaterialExpressionScalarParameter, parameter_name="Tile", default_value=tile)
    tc = g.node(unreal.MaterialExpressionVectorParameter, parameter_name="Tint", default_value=unreal.LinearColor(tint[0], tint[1], tint[2], 1))
    rm = g.node(unreal.MaterialExpressionScalarParameter, parameter_name="RoughMul", default_value=rough)
    mt = g.node(unreal.MaterialExpressionScalarParameter, parameter_name="Metal", default_value=metal)
    common = [("WP", (wp, "")), ("Nw", (nw, "")), ("Tile", (ti, ""))]
    col = g.custom(COL, common + [("Diff", (td, "")), ("Tint", (tc, ""))], F3, "TriColor")
    armn = g.custom(ARM, common + [("Arm", (ta, "")), ("RoughMul", (rm, "")), ("Metal", (mt, ""))], F3, "TriARM")
    nrm = g.custom(NRM, common + [("Nor", (tn, ""))], F3, "TriNormal")
    masks = []
    for ch in ("r", "g", "b"):
        m = g.node(unreal.MaterialExpressionComponentMask, r=ch == "r", g=ch == "g", b=ch == "b", a=False)
        MEL.connect_material_expressions(armn, "", m, "")
        masks.append(m)
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(masks[0], "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    MEL.connect_material_property(masks[1], "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(masks[2], "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(nrm, "", unreal.MaterialProperty.MP_NORMAL)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("material", name)


def flat(fresh, Graph, log, name, color, rough, emissive=None, metal=0.0):
    mat = fresh(name, unreal.Material, unreal.MaterialFactoryNew(), MAT)
    g = Graph(mat)
    c = g.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(*color))
    r = g.node(unreal.MaterialExpressionConstant, r=rough)
    m = g.node(unreal.MaterialExpressionConstant, r=metal)
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(m, "", unreal.MaterialProperty.MP_METALLIC)
    if emissive:
        e = g.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(*emissive))
        MEL.connect_material_property(e, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("material", name)


def marker(fresh, Graph, log):
    # painted bomb-site square: orange paint with worn stripes, slightly emissive so it reads in shade
    mat = fresh("M_Marker", unreal.Material, unreal.MaterialFactoryNew(), MAT)
    g = Graph(mat)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    col = g.custom("float s = frac((WP.x + WP.y) / 60.0); float stripe = step(0.5, s); return lerp(float3(0.55, 0.22, 0.03), float3(0.06, 0.06, 0.06), stripe);",
                   [("WP", (wp, ""))], F3, "Stripes")
    emi = g.custom("float s = frac((WP.x + WP.y) / 60.0); return step(s, 0.5) * float3(0.25, 0.09, 0.01);", [("WP", (wp, ""))], F3, "Glow")
    r = g.node(unreal.MaterialExpressionConstant, r=0.7)
    MEL.connect_material_property(col, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(emi, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("material M_Marker")


def grate(fresh, Graph, log):
    # steel grate: masked bars, so smoke and light pass the gaps (bullets and players still collide with the box)
    mat = fresh("M_Grate", unreal.Material, unreal.MaterialFactoryNew(), MAT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    g = Graph(mat)
    wp = g.node(unreal.MaterialExpressionWorldPosition)
    op = g.custom("float2 p = float2(WP.x + WP.y, WP.z) / 14.0; float2 f = abs(frac(p) - 0.5); return (f.x > 0.36 || f.y > 0.40) ? 1.0 : 0.0;", [("WP", (wp, ""))], F1, "Bars")
    c = g.node(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0.08, 0.075, 0.07, 1))
    m = g.node(unreal.MaterialExpressionConstant, r=1.0)
    r = g.node(unreal.MaterialExpressionConstant, r=0.55)
    MEL.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(m, "", unreal.MaterialProperty.MP_METALLIC)
    MEL.connect_material_property(r, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    log("material M_Grate")


PROPS = ["Barrel_01", "barrel_03", "old_military_crate", "concrete_road_barrier", "portable_generator", "large_iron_gate",
         "caged_hanging_light", "industrial_wall_lamp", "plastic_crate_01", "cardboard_box_01", "metal_jerrycan", "ammo_box", "power_box_01"]


def import_prop(name, log):
    dest = "/Game/SmokeLab/Props"
    sm_path = f"{dest}/SM_{name}"
    if EAL.does_asset_exist(sm_path):
        return
    d = os.path.join(SRC, "models", name)
    gltf = [f for f in os.listdir(d) if f.endswith(".gltf")]
    if not gltf:
        log("no gltf for", name)
        return
    sub = f"{dest}/{name}"
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(d, gltf[0]))
    t.set_editor_property("destination_path", sub)
    t.set_editor_property("automated", True)
    t.set_editor_property("save", False)
    t.set_editor_property("replace_existing", True)
    AT.import_asset_tasks([t])
    meshes = sorted(a for a in EAL.list_assets(sub, recursive=True) if EAL.find_asset_data(a).asset_class_path.asset_name == "StaticMesh")
    if not meshes:
        log("import produced no static mesh", name)
        return
    for i, m in enumerate(meshes):
        sm = unreal.load_asset(m)
        bs = sm.get_editor_property("body_setup")
        if bs:
            bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        EAL.save_loaded_asset(sm)
        EAL.rename_asset(m.split(".")[0], sm_path if i == 0 else f"{sm_path}_{i}")
    log("prop", name, "parts", len(meshes))


def run(fresh, Graph, log, build):
    if not os.path.isdir(os.path.join(SRC, "textures")):
        raise ImportError("no SourceAssets")
    build(triplanar, fresh, Graph, log, "M_Concrete", "concrete_layers_02", 260.0, tint=(0.92, 0.9, 0.86))
    build(triplanar, fresh, Graph, log, "M_Plaster", "concrete_moss", 300.0, tint=(1.05, 1.0, 0.92))
    build(triplanar, fresh, Graph, log, "M_Brick", "brick_wall_006", 240.0)
    build(triplanar, fresh, Graph, log, "M_Floor", "concrete_layers_02", 180.0, tint=(0.62, 0.6, 0.57))
    build(triplanar, fresh, Graph, log, "M_Ground", "mossy_brick", 200.0, tint=(0.8, 0.78, 0.74))
    build(triplanar, fresh, Graph, log, "M_Roof", "rusty_corrugated_iron", 200.0, metal=1.0)
    build(triplanar, fresh, Graph, log, "M_Wood", "weathered_brown_planks", 160.0)
    build(triplanar, fresh, Graph, log, "M_Metal", "rusty_metal_02", 150.0, metal=1.0)
    build(triplanar, fresh, Graph, log, "M_Trim", "rusty_metal_02", 120.0, tint=(0.5, 0.5, 0.5), metal=1.0)
    build(flat, fresh, Graph, log, "M_Grenade", (0.12, 0.16, 0.08), 0.5, metal=0.3)
    build(flat, fresh, Graph, log, "M_Gun", (0.07, 0.07, 0.075), 0.42, metal=0.5)
    build(marker, fresh, Graph, log)
    build(grate, fresh, Graph, log)
    for p in PROPS:
        import_prop(p, log)
    import_audio(log)


# CC0 recordings from OpenGameArt, trimmed and mixed with ffmpeg (see README); everything but the player's own shot
# is spatialised through one shared attenuation.
SOUNDS = [("S_RifleShot", False), ("S_RifleShotFar", True), ("S_Explosion", True), ("S_SmokePop", True)]


def import_audio(log):
    dest = "/Game/SmokeLab/Audio"
    att_path = f"{dest}/ATT_World"
    if EAL.does_asset_exist(att_path):
        att = unreal.load_asset(att_path)
    else:
        att = AT.create_asset("ATT_World", dest, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
        s = att.get_editor_property("attenuation")
        s.set_editor_property("attenuation_shape_extents", unreal.Vector(400.0, 0.0, 0.0))
        s.set_editor_property("falloff_distance", 7000.0)
        s.set_editor_property("distance_algorithm", unreal.AttenuationDistanceModel.NATURAL_SOUND)
        att.set_editor_property("attenuation", s)
        EAL.save_loaded_asset(att)
    for name, spatial in SOUNDS:
        path = f"{dest}/{name}"
        if EAL.does_asset_exist(path):
            continue
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", os.path.join(SRC, "audio", name + ".wav"))
        t.set_editor_property("destination_path", dest)
        t.set_editor_property("automated", True)
        t.set_editor_property("save", False)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("factory", unreal.SoundFactory())
        AT.import_asset_tasks([t])
        w = unreal.load_asset(path)
        if w is None:
            log("audio import failed", name)
            continue
        if spatial:
            w.set_editor_property("attenuation_settings", att)
        EAL.save_loaded_asset(w)
        log("audio", name)
