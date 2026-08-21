// ============================================================================
//   我的世界方块模型库 + 预拼图集 + 合并网格（Round194）
//   纯自包含：无第三方库；贴图用 WIC 解码（png）；合并用贪心网格算法
//   图集来源：用户提供的预拼目录（ss1-RGB.png + ss1.mtl），非运行时扫描/程序化
// ============================================================================
#include "mc_blocks.h"
#include "mc_blockmap.h"
#include "mc_block_colors.h"   // 透明方块集合（MCA color_properties 移植）

#include <windows.h>
#include <objbase.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

// ----------------------------- 工具 -----------------------------
static std::string ToLower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = (char)::tolower((unsigned char)c);
    return r;
}

// ----------------------------- WIC 解码 -----------------------------
static IWICImagingFactory* g_wicFactory = nullptr;
static bool EnsureWic() {
    if (g_wicFactory) return true;
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IWICImagingFactory, (void**)&g_wicFactory);
    return SUCCEEDED(hr) && g_wicFactory;
}

static bool WicDecode(const wchar_t* path, std::vector<uint8_t>& rgba, int& w, int& h) {
    if (!EnsureWic()) return false;
    IWICBitmapDecoder* dec = nullptr;
    HRESULT hr = g_wicFactory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                                         WICDecodeMetadataCacheOnLoad, &dec);
    if (FAILED(hr) || !dec) return false;
    IWICBitmapFrameDecode* frame = nullptr;
    if (FAILED(dec->GetFrame(0, &frame)) || !frame) { dec->Release(); return false; }
    UINT ww = 0, hh = 0; frame->GetSize(&ww, &hh);
    IWICFormatConverter* conv = nullptr;
    g_wicFactory->CreateFormatConverter(&conv);
    bool ok = false;
    if (conv) {
        if (SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                      WICBitmapDitherTypeNone, nullptr, 0.0f,
                                      WICBitmapPaletteTypeMedianCut))) {
            rgba.resize((size_t)ww * hh * 4);
            if (SUCCEEDED(conv->CopyPixels(nullptr, ww * 4, (UINT)rgba.size(), rgba.data()))) {
                w = (int)ww; h = (int)hh; ok = true;
            }
        }
        conv->Release();
    }
    frame->Release(); dec->Release();
    return ok;
}

// ----------------------------- 方块注册表（1.12 数字 id + 1.13+ 字符串 id 统一）-----------------------------
#define MC(key, leg, t)        { key, leg, { t, t, t, t, t, t } }
#define M6(key, leg, tp,tn,ts,te,tw,tb) { key, leg, { tp, tn, ts, te, tw, tb } }

static std::vector<BlockDef> BuildRegistry() {
    static const BlockDef raw[] = {
        MC("minecraft:air", -1, "air"),
        MC("minecraft:stone", 1, "stone"),
        MC("minecraft:granite", -1, "granite"),
        MC("minecraft:polished_granite", -1, "polished_granite"),
        MC("minecraft:diorite", -1, "diorite"),
        MC("minecraft:polished_diorite", -1, "polished_diorite"),
        MC("minecraft:andesite", -1, "andesite"),
        MC("minecraft:polished_andesite", -1, "polished_andesite"),
        M6("minecraft:grass_block", 2, "grass_block_top", "dirt", "grass_block_side", "grass_block_side", "grass_block_side", "grass_block_side"),
        MC("minecraft:dirt", 3, "dirt"),
        MC("minecraft:coarse_dirt", -1, "coarse_dirt"),
        M6("minecraft:podzol", -1, "podzol_top", "dirt", "podzol_side", "podzol_side", "podzol_side", "podzol_side"),
        MC("minecraft:rooted_dirt", -1, "rooted_dirt"),
        MC("minecraft:cobblestone", 4, "cobblestone"),
        MC("minecraft:mossy_cobblestone", 48, "mossy_cobblestone"),
        MC("minecraft:bedrock", 7, "bedrock"),
        MC("minecraft:sand", 12, "sand"),
        MC("minecraft:red_sand", -1, "red_sand"),
        M6("minecraft:sandstone", 24, "sandstone_top", "sandstone_bottom", "sandstone", "sandstone", "sandstone", "sandstone"),
        MC("minecraft:chiseled_sandstone", -1, "chiseled_sandstone"),
        MC("minecraft:cut_sandstone", -1, "cut_sandstone"),
        MC("minecraft:smooth_sandstone", -1, "smooth_sandstone"),
        MC("minecraft:gravel", 13, "gravel"),
        M6("minecraft:oak_log", 17, "oak_log_top", "oak_log_top", "oak_log", "oak_log", "oak_log", "oak_log"),
        M6("minecraft:spruce_log", -1, "spruce_log_top", "spruce_log_top", "spruce_log", "spruce_log", "spruce_log", "spruce_log"),
        M6("minecraft:birch_log", -1, "birch_log_top", "birch_log_top", "birch_log", "birch_log", "birch_log", "birch_log"),
        M6("minecraft:jungle_log", -1, "jungle_log_top", "jungle_log_top", "jungle_log", "jungle_log", "jungle_log", "jungle_log"),
        M6("minecraft:acacia_log", -1, "acacia_log_top", "acacia_log_top", "acacia_log", "acacia_log", "acacia_log", "acacia_log"),
        M6("minecraft:dark_oak_log", -1, "dark_oak_log_top", "dark_oak_log_top", "dark_oak_log", "dark_oak_log", "dark_oak_log", "dark_oak_log"),
        MC("minecraft:oak_planks", 5, "oak_planks"),
        MC("minecraft:spruce_planks", -1, "spruce_planks"),
        MC("minecraft:birch_planks", -1, "birch_planks"),
        MC("minecraft:jungle_planks", -1, "jungle_planks"),
        MC("minecraft:acacia_planks", -1, "acacia_planks"),
        MC("minecraft:dark_oak_planks", -1, "dark_oak_planks"),
        MC("minecraft:oak_leaves", 18, "oak_leaves"),
        MC("minecraft:spruce_leaves", -1, "spruce_leaves"),
        MC("minecraft:birch_leaves", -1, "birch_leaves"),
        MC("minecraft:jungle_leaves", -1, "jungle_leaves"),
        MC("minecraft:acacia_leaves", -1, "acacia_leaves"),
        MC("minecraft:dark_oak_leaves", -1, "dark_oak_leaves"),
        MC("minecraft:glass", 20, "glass"),
        MC("minecraft:white_stained_glass", -1, "white_stained_glass"),
        MC("minecraft:orange_stained_glass", -1, "orange_stained_glass"),
        MC("minecraft:blue_stained_glass", -1, "blue_stained_glass"),
        MC("minecraft:water", 8, "water"),
        MC("minecraft:lava", 10, "lava"),
        MC("minecraft:gold_ore", 14, "gold_ore"),
        MC("minecraft:iron_ore", 15, "iron_ore"),
        MC("minecraft:coal_ore", 16, "coal_ore"),
        MC("minecraft:lapis_ore", 21, "lapis_ore"),
        MC("minecraft:lapis_block", 22, "lapis_block"),
        MC("minecraft:diamond_ore", 56, "diamond_ore"),
        MC("minecraft:diamond_block", 57, "diamond_block"),
        MC("minecraft:redstone_ore", 73, "redstone_ore"),
        MC("minecraft:emerald_ore", 129, "emerald_ore"),
        MC("minecraft:emerald_block", 133, "emerald_block"),
        MC("minecraft:copper_ore", -1, "copper_ore"),
        MC("minecraft:copper_block", -1, "copper_block"),
        MC("minecraft:nether_gold_ore", -1, "nether_gold_ore"),
        MC("minecraft:ancient_debris", -1, "ancient_debris"),
        MC("minecraft:netherite_block", -1, "netherite_block"),
        MC("minecraft:gold_block", 41, "gold_block"),
        MC("minecraft:iron_block", 42, "iron_block"),
        MC("minecraft:brick", 45, "brick"),
        MC("minecraft:stone_bricks", -1, "stone_bricks"),
        MC("minecraft:mossy_stone_bricks", -1, "mossy_stone_bricks"),
        MC("minecraft:cracked_stone_bricks", -1, "cracked_stone_bricks"),
        MC("minecraft:chiseled_stone_bricks", -1, "chiseled_stone_bricks"),
        MC("minecraft:bookshelf", 47, "bookshelf"),
        MC("minecraft:obsidian", 49, "obsidian"),
        MC("minecraft:crying_obsidian", -1, "crying_obsidian"),
        MC("minecraft:glowstone", 89, "glowstone"),
        MC("minecraft:sea_lantern", -1, "sea_lantern"),
        MC("minecraft:shroomlight", -1, "shroomlight"),
        MC("minecraft:netherrack", 87, "netherrack"),
        MC("minecraft:soul_sand", 88, "soul_sand"),
        MC("minecraft:soul_soil", -1, "soul_soil"),
        MC("minecraft:basalt", -1, "basalt"),
        MC("minecraft:blackstone", -1, "blackstone"),
        MC("minecraft:gilded_blackstone", -1, "gilded_blackstone"),
        MC("minecraft:polished_basalt", -1, "polished_basalt"),
        MC("minecraft:end_stone", 121, "end_stone"),
        MC("minecraft:purpur_block", -1, "purpur_block"),
        MC("minecraft:purpur_pillar", -1, "purpur_pillar"),
        MC("minecraft:quartz_block", 155, "quartz_block"),
        MC("minecraft:quartz_pillar", -1, "quartz_pillar"),
        MC("minecraft:chiseled_quartz_block", -1, "chiseled_quartz_block"),
        MC("minecraft:smooth_quartz", -1, "smooth_quartz"),
        MC("minecraft:quartz_bricks", -1, "quartz_bricks"),
        MC("minecraft:prismarine", -1, "prismarine"),
        MC("minecraft:prismarine_bricks", -1, "prismarine_bricks"),
        MC("minecraft:dark_prismarine", -1, "dark_prismarine"),
        MC("minecraft:clay", 82, "clay"),
        MC("minecraft:terracotta", -1, "terracotta"),
        MC("minecraft:white_terracotta", -1, "white_terracotta"),
        MC("minecraft:orange_terracotta", -1, "orange_terracotta"),
        MC("minecraft:red_terracotta", -1, "red_terracotta"),
        MC("minecraft:blue_terracotta", -1, "blue_terracotta"),
        MC("minecraft:white_concrete", -1, "white_concrete"),
        MC("minecraft:orange_concrete", -1, "orange_concrete"),
        MC("minecraft:red_concrete", -1, "red_concrete"),
        MC("minecraft:blue_concrete", -1, "blue_concrete"),
        MC("minecraft:white_wool", 35, "white_wool"),
        MC("minecraft:orange_wool", -1, "orange_wool"),
        MC("minecraft:red_wool", -1, "red_wool"),
        MC("minecraft:blue_wool", -1, "blue_wool"),
        MC("minecraft:ice", 79, "ice"),
        MC("minecraft:packed_ice", -1, "packed_ice"),
        MC("minecraft:blue_ice", -1, "blue_ice"),
        MC("minecraft:snow_block", -1, "snow_block"),
        MC("minecraft:snow", -1, "snow"),
        MC("minecraft:slime_block", -1, "slime_block"),
        MC("minecraft:honey_block", -1, "honey_block"),
        MC("minecraft:hay_block", -1, "hay_block"),
        MC("minecraft:dried_kelp_block", -1, "dried_kelp_block"),
        MC("minecraft:melon", 103, "melon"),
        MC("minecraft:pumpkin", 86, "pumpkin"),
        MC("minecraft:carved_pumpkin", -1, "carved_pumpkin"),
        MC("minecraft:jack_o_lantern", 91, "jack_o_lantern"),
        MC("minecraft:tnt", 46, "tnt"),
        MC("minecraft:sponge", 19, "sponge"),
        MC("minecraft:wet_sponge", -1, "wet_sponge"),
        MC("minecraft:magma_block", -1, "magma_block"),
        M6("minecraft:crafting_table", 58, "crafting_table_top", "crafting_table_top", "crafting_table_side", "crafting_table_side", "crafting_table_front", "crafting_table_side"),
        M6("minecraft:furnace", 61, "furnace_top", "furnace_top", "furnace_side", "furnace_side", "furnace_side", "furnace_front"),
        M6("minecraft:smoker", -1, "smoker_top", "smoker_top", "smoker_side", "smoker_side", "smoker_side", "smoker_front"),
        M6("minecraft:blast_furnace", -1, "blast_furnace_top", "blast_furnace_top", "blast_furnace_side", "blast_furnace_side", "blast_furnace_side", "blast_furnace_front"),
        M6("minecraft:barrel", -1, "barrel_top", "barrel_top", "barrel_side", "barrel_side", "barrel_side", "barrel_side"),
        M6("minecraft:dispenser", 23, "dispenser_front", "dispenser_front", "dispenser_side", "dispenser_side", "dispenser_side", "dispenser_side"),
        M6("minecraft:dropper", -1, "dropper_front", "dropper_front", "dropper_side", "dropper_side", "dropper_side", "dropper_side"),
        MC("minecraft:chest", 54, "chest"),
        MC("minecraft:note_block", 25, "note_block"),
        MC("minecraft:jukebox", -1, "jukebox_top"),
        MC("minecraft:cobbled_deepslate", -1, "cobbled_deepslate"),
        MC("minecraft:deepslate", -1, "deepslate"),
        MC("minecraft:polished_deepslate", -1, "polished_deepslate"),
        MC("minecraft:deepslate_bricks", -1, "deepslate_bricks"),
        MC("minecraft:cracked_nether_bricks", -1, "cracked_nether_bricks"),
        MC("minecraft:chiseled_nether_bricks", -1, "chiseled_nether_bricks"),
        MC("minecraft:nether_bricks", 112, "nether_bricks"),
        MC("minecraft:red_nether_bricks", -1, "red_nether_bricks"),
        MC("minecraft:bone_block", -1, "bone_block_top"),
        MC("minecraft:amethyst_block", -1, "amethyst_block"),
        MC("minecraft:tinted_glass", -1, "tinted_glass"),
        MC("minecraft:moss_block", -1, "moss_block"),
        MC("minecraft:dripstone_block", -1, "dripstone_block"),
        MC("minecraft:target", -1, "target"),
        MC("minecraft:respawn_anchor", -1, "respawn_anchor"),
        MC("minecraft:lodestone", -1, "lodestone"),
        MC("minecraft:chain", -1, "chain"),
        MC("minecraft:conduit", -1, "conduit"),
        MC("minecraft:honeycomb_block", -1, "honeycomb_block"),
        MC("minecraft:bee_nest", -1, "bee_nest"),
        MC("minecraft:beehive", -1, "beehive"),
        MC("minecraft:lantern", -1, "lantern"),
        MC("minecraft:campfire", -1, "campfire"),
        MC("minecraft:soul_campfire", -1, "soul_campfire"),
        MC("minecraft:bell", -1, "bell"),
        MC("minecraft:grindstone", -1, "grindstone"),
        MC("minecraft:anvil", -1, "anvil"),
        MC("minecraft:enchanting_table", -1, "enchanting_table"),
        MC("minecraft:end_portal_frame", -1, "end_portal_frame"),
        MC("minecraft:ender_chest", -1, "ender_chest"),
        MC("minecraft:dragon_egg", -1, "dragon_egg"),
        MC("minecraft:spawner", 52, "spawner"),
        M6("minecraft:piston", -1, "piston_top", "piston_bottom", "piston_side", "piston_side", "piston_side", "piston_top"),
        MC("minecraft:sticky_piston", -1, "sticky_piston"),
        MC("minecraft:redstone_block", -1, "redstone_block"),
        MC("minecraft:redstone_lamp", -1, "redstone_lamp"),
        MC("minecraft:observer", -1, "observer"),
        MC("minecraft:hopper", -1, "hopper"),
        MC("minecraft:warped_planks", -1, "warped_planks"),
        MC("minecraft:crimson_planks", -1, "crimson_planks"),
        M6("minecraft:warped_stem", -1, "warped_stem_top", "warped_stem_top", "warped_stem", "warped_stem", "warped_stem", "warped_stem"),
        M6("minecraft:crimson_stem", -1, "crimson_stem_top", "crimson_stem_top", "crimson_stem", "crimson_stem", "crimson_stem", "crimson_stem"),
        M6("minecraft:warped_nylium", -1, "warped_nylium_top", "warped_nylium_side", "warped_nylium_side", "warped_nylium_side", "warped_nylium_side", "warped_nylium_side"),
        M6("minecraft:crimson_nylium", -1, "crimson_nylium_top", "crimson_nylium_side", "crimson_nylium_side", "crimson_nylium_side", "crimson_nylium_side", "crimson_nylium_side"),
        MC("minecraft:warped_wart_block", -1, "warped_wart_block"),
        MC("minecraft:nether_wart_block", -1, "nether_wart_block"),
        MC("minecraft:smooth_stone", -1, "smooth_stone"),

        // ================= 1.16 下界更新补充：树皮/去皮变体 =================
        M6("minecraft:oak_wood", -1, "oak_log", "oak_log", "oak_log", "oak_log", "oak_log", "oak_log"),
        M6("minecraft:spruce_wood", -1, "spruce_log", "spruce_log", "spruce_log", "spruce_log", "spruce_log", "spruce_log"),
        M6("minecraft:birch_wood", -1, "birch_log", "birch_log", "birch_log", "birch_log", "birch_log", "birch_log"),
        M6("minecraft:jungle_wood", -1, "jungle_log", "jungle_log", "jungle_log", "jungle_log", "jungle_log", "jungle_log"),
        M6("minecraft:acacia_wood", -1, "acacia_log", "acacia_log", "acacia_log", "acacia_log", "acacia_log", "acacia_log"),
        M6("minecraft:dark_oak_wood", -1, "dark_oak_log", "dark_oak_log", "dark_oak_log", "dark_oak_log", "dark_oak_log", "dark_oak_log"),
        M6("minecraft:stripped_oak_log", -1, "stripped_oak_log", "stripped_oak_log", "stripped_oak_log", "stripped_oak_log", "stripped_oak_log", "stripped_oak_log"),
        M6("minecraft:stripped_oak_wood", -1, "stripped_oak_log", "stripped_oak_log", "stripped_oak_log", "stripped_oak_log", "stripped_oak_log", "stripped_oak_log"),
        M6("minecraft:stripped_spruce_log", -1, "stripped_spruce_log", "stripped_spruce_log", "stripped_spruce_log", "stripped_spruce_log", "stripped_spruce_log", "stripped_spruce_log"),
        M6("minecraft:stripped_birch_log", -1, "stripped_birch_log", "stripped_birch_log", "stripped_birch_log", "stripped_birch_log", "stripped_birch_log", "stripped_birch_log"),
        M6("minecraft:stripped_jungle_log", -1, "stripped_jungle_log", "stripped_jungle_log", "stripped_jungle_log", "stripped_jungle_log", "stripped_jungle_log", "stripped_jungle_log"),
        M6("minecraft:stripped_acacia_log", -1, "stripped_acacia_log", "stripped_acacia_log", "stripped_acacia_log", "stripped_acacia_log", "stripped_acacia_log", "stripped_acacia_log"),
        M6("minecraft:stripped_dark_oak_log", -1, "stripped_dark_oak_log", "stripped_dark_oak_log", "stripped_dark_oak_log", "stripped_dark_oak_log", "stripped_dark_oak_log", "stripped_dark_oak_log"),
        MC("minecraft:crimson_hyphae", -1, "crimson_stem"),
        MC("minecraft:warped_hyphae", -1, "warped_stem"),
        MC("minecraft:stripped_crimson_stem", -1, "crimson_stem"),
        MC("minecraft:stripped_warped_stem", -1, "warped_stem"),
        MC("minecraft:stripped_crimson_hyphae", -1, "crimson_stem"),
        MC("minecraft:stripped_warped_hyphae", -1, "warped_stem"),
        MC("minecraft:nether_wart", -1, "nether_wart"),
        MC("minecraft:warped_roots", -1, "warped_roots"),
        MC("minecraft:crimson_roots", -1, "crimson_roots"),
        MC("minecraft:nether_sprouts", -1, "nether_sprouts"),
        MC("minecraft:weeping_vines", -1, "weeping_vines"),
        MC("minecraft:twisting_vines", -1, "twisting_vines"),
        MC("minecraft:soul_fire", -1, "soul_fire"),
        MC("minecraft:soul_lantern", -1, "soul_lantern"),
        MC("minecraft:soul_torch", -1, "soul_torch"),

        // ================= 1.17 洞穴与山崖 补充 =================
        MC("minecraft:deepslate_copper_ore", -1, "deepslate_copper_ore"),
        MC("minecraft:deepslate_iron_ore", -1, "deepslate_iron_ore"),
        MC("minecraft:deepslate_gold_ore", -1, "deepslate_gold_ore"),
        MC("minecraft:deepslate_coal_ore", -1, "deepslate_coal_ore"),
        MC("minecraft:deepslate_lapis_ore", -1, "deepslate_lapis_ore"),
        MC("minecraft:deepslate_diamond_ore", -1, "deepslate_diamond_ore"),
        MC("minecraft:deepslate_redstone_ore", -1, "deepslate_redstone_ore"),
        MC("minecraft:deepslate_emerald_ore", -1, "deepslate_emerald_ore"),
        MC("minecraft:raw_iron_block", -1, "raw_iron_block"),
        MC("minecraft:raw_copper_block", -1, "raw_copper_block"),
        MC("minecraft:raw_gold_block", -1, "raw_gold_block"),
        MC("minecraft:calcite", -1, "calcite"),
        MC("minecraft:tuff", -1, "tuff"),
        MC("minecraft:smooth_basalt", -1, "smooth_basalt"),
        MC("minecraft:amethyst_cluster", -1, "amethyst_cluster"),
        MC("minecraft:budding_amethyst", -1, "budding_amethyst"),
        MC("minecraft:small_amethyst_bud", -1, "small_amethyst_bud"),
        MC("minecraft:medium_amethyst_bud", -1, "medium_amethyst_bud"),
        MC("minecraft:large_amethyst_bud", -1, "large_amethyst_bud"),
        MC("minecraft:glow_lichen", -1, "glow_lichen"),
        MC("minecraft:spore_blossom", -1, "spore_blossom"),
        MC("minecraft:azalea", -1, "azalea"),
        MC("minecraft:flowering_azalea", -1, "flowering_azalea"),
        MC("minecraft:azalea_leaves", -1, "azalea_leaves"),
        MC("minecraft:flowering_azalea_leaves", -1, "flowering_azalea_leaves"),
        MC("minecraft:hanging_roots", -1, "hanging_roots"),
        MC("minecraft:small_dripleaf", -1, "small_dripleaf"),
        MC("minecraft:big_dripleaf", -1, "big_dripleaf"),
        MC("minecraft:powder_snow", -1, "powder_snow"),
        MC("minecraft:sculk_sensor", -1, "sculk_sensor"),

        // ================= 1.19 荒野更新 =================
        M6("minecraft:mangrove_log", -1, "mangrove_log_top", "mangrove_log_top", "mangrove_log", "mangrove_log", "mangrove_log", "mangrove_log"),
        M6("minecraft:mangrove_wood", -1, "mangrove_log", "mangrove_log", "mangrove_log", "mangrove_log", "mangrove_log", "mangrove_log"),
        M6("minecraft:stripped_mangrove_log", -1, "stripped_mangrove_log", "stripped_mangrove_log", "stripped_mangrove_log", "stripped_mangrove_log", "stripped_mangrove_log", "stripped_mangrove_log"),
        M6("minecraft:stripped_mangrove_wood", -1, "stripped_mangrove_log", "stripped_mangrove_log", "stripped_mangrove_log", "stripped_mangrove_log", "stripped_mangrove_log", "stripped_mangrove_log"),
        MC("minecraft:mangrove_planks", -1, "mangrove_planks"),
        MC("minecraft:mangrove_leaves", -1, "mangrove_leaves"),
        MC("minecraft:mangrove_roots", -1, "mangrove_roots"),
        MC("minecraft:muddy_mangrove_roots", -1, "muddy_mangrove_roots"),
        MC("minecraft:mangrove_propagule", -1, "mangrove_propagule"),
        MC("minecraft:mud", -1, "mud"),
        MC("minecraft:packed_mud", -1, "packed_mud"),
        MC("minecraft:mud_bricks", -1, "mud_bricks"),
        MC("minecraft:sculk", -1, "sculk"),
        MC("minecraft:sculk_catalyst", -1, "sculk_catalyst"),
        MC("minecraft:sculk_shrieker", -1, "sculk_shrieker"),
        MC("minecraft:sculk_vein", -1, "sculk_vein"),
        MC("minecraft:reinforced_deepslate", -1, "reinforced_deepslate"),
        MC("minecraft:frogspawn", -1, "frogspawn"),
        MC("minecraft:ochre_froglight", -1, "ochre_froglight"),
        MC("minecraft:verdant_froglight", -1, "verdant_froglight"),
        MC("minecraft:pearlescent_froglight", -1, "pearlescent_froglight"),

        // ================= 1.20 足迹与故事 =================
        M6("minecraft:cherry_log", -1, "cherry_log_top", "cherry_log_top", "cherry_log", "cherry_log", "cherry_log", "cherry_log"),
        M6("minecraft:cherry_wood", -1, "cherry_log", "cherry_log", "cherry_log", "cherry_log", "cherry_log", "cherry_log"),
        M6("minecraft:stripped_cherry_log", -1, "stripped_cherry_log", "stripped_cherry_log", "stripped_cherry_log", "stripped_cherry_log", "stripped_cherry_log", "stripped_cherry_log"),
        M6("minecraft:stripped_cherry_wood", -1, "stripped_cherry_log", "stripped_cherry_log", "stripped_cherry_log", "stripped_cherry_log", "stripped_cherry_log", "stripped_cherry_log"),
        MC("minecraft:cherry_planks", -1, "cherry_planks"),
        MC("minecraft:cherry_leaves", -1, "cherry_leaves"),
        MC("minecraft:cherry_sapling", -1, "cherry_sapling"),
        MC("minecraft:pink_petals", -1, "pink_petals"),
        M6("minecraft:bamboo_block", -1, "bamboo_block_top", "bamboo_block_top", "bamboo_block", "bamboo_block", "bamboo_block", "bamboo_block"),
        M6("minecraft:stripped_bamboo_block", -1, "stripped_bamboo_block", "stripped_bamboo_block", "stripped_bamboo_block", "stripped_bamboo_block", "stripped_bamboo_block", "stripped_bamboo_block"),
        MC("minecraft:bamboo_planks", -1, "bamboo_planks"),
        MC("minecraft:bamboo_mosaic", -1, "bamboo_mosaic"),
        MC("minecraft:chiseled_bookshelf", -1, "chiseled_bookshelf"),
        MC("minecraft:decorated_pot", -1, "decorated_pot"),
        MC("minecraft:suspicious_sand", -1, "suspicious_sand"),
        MC("minecraft:suspicious_gravel", -1, "suspicious_gravel"),
        MC("minecraft:calibrated_sculk_sensor", -1, "calibrated_sculk_sensor"),
        MC("minecraft:torchflower", -1, "torchflower"),
        MC("minecraft:pitcher_plant", -1, "pitcher_plant"),
        MC("minecraft:sniffer_egg", -1, "sniffer_egg"),

        // ================= 1.21 试炼密室 =================
        MC("minecraft:crafter", -1, "crafter"),
        MC("minecraft:trial_spawner", -1, "trial_spawner"),
        MC("minecraft:ominous_trial_spawner", -1, "ominous_trial_spawner"),
        MC("minecraft:vault", -1, "vault"),
        MC("minecraft:ominous_vault", -1, "ominous_vault"),
        MC("minecraft:heavy_core", -1, "heavy_core"),
        // 铜系列（含氧化/打蜡 8 变体，共用铜纹理）
        MC("minecraft:chiseled_copper", -1, "chiseled_copper"),
        MC("minecraft:exposed_chiseled_copper", -1, "exposed_chiseled_copper"),
        MC("minecraft:weathered_chiseled_copper", -1, "weathered_chiseled_copper"),
        MC("minecraft:oxidized_chiseled_copper", -1, "oxidized_chiseled_copper"),
        MC("minecraft:waxed_chiseled_copper", -1, "waxed_chiseled_copper"),
        MC("minecraft:waxed_exposed_chiseled_copper", -1, "waxed_exposed_chiseled_copper"),
        MC("minecraft:waxed_weathered_chiseled_copper", -1, "waxed_weathered_chiseled_copper"),
        MC("minecraft:waxed_oxidized_chiseled_copper", -1, "waxed_oxidized_chiseled_copper"),
        MC("minecraft:copper_bulb", -1, "copper_bulb"),
        MC("minecraft:exposed_copper_bulb", -1, "exposed_copper_bulb"),
        MC("minecraft:weathered_copper_bulb", -1, "weathered_copper_bulb"),
        MC("minecraft:oxidized_copper_bulb", -1, "oxidized_copper_bulb"),
        MC("minecraft:waxed_copper_bulb", -1, "waxed_copper_bulb"),
        MC("minecraft:waxed_exposed_copper_bulb", -1, "waxed_exposed_copper_bulb"),
        MC("minecraft:waxed_weathered_copper_bulb", -1, "waxed_weathered_copper_bulb"),
        MC("minecraft:waxed_oxidized_copper_bulb", -1, "waxed_oxidized_copper_bulb"),
        MC("minecraft:copper_grate", -1, "copper_grate"),
        MC("minecraft:exposed_copper_grate", -1, "exposed_copper_grate"),
        MC("minecraft:weathered_copper_grate", -1, "weathered_copper_grate"),
        MC("minecraft:oxidized_copper_grate", -1, "oxidized_copper_grate"),
        MC("minecraft:waxed_copper_grate", -1, "waxed_copper_grate"),
        MC("minecraft:waxed_exposed_copper_grate", -1, "waxed_exposed_copper_grate"),
        MC("minecraft:waxed_weathered_copper_grate", -1, "waxed_weathered_copper_grate"),
        MC("minecraft:waxed_oxidized_copper_grate", -1, "waxed_oxidized_copper_grate"),
        MC("minecraft:copper_door", -1, "copper_door"),
        MC("minecraft:exposed_copper_door", -1, "exposed_copper_door"),
        MC("minecraft:weathered_copper_door", -1, "weathered_copper_door"),
        MC("minecraft:oxidized_copper_door", -1, "oxidized_copper_door"),
        MC("minecraft:waxed_copper_door", -1, "waxed_copper_door"),
        MC("minecraft:waxed_exposed_copper_door", -1, "waxed_exposed_copper_door"),
        MC("minecraft:waxed_weathered_copper_door", -1, "waxed_weathered_copper_door"),
        MC("minecraft:waxed_oxidized_copper_door", -1, "waxed_oxidized_copper_door"),
        MC("minecraft:copper_trapdoor", -1, "copper_trapdoor"),
        MC("minecraft:exposed_copper_trapdoor", -1, "exposed_copper_trapdoor"),
        MC("minecraft:weathered_copper_trapdoor", -1, "weathered_copper_trapdoor"),
        MC("minecraft:oxidized_copper_trapdoor", -1, "oxidized_copper_trapdoor"),
        MC("minecraft:waxed_copper_trapdoor", -1, "waxed_copper_trapdoor"),
        MC("minecraft:waxed_exposed_copper_trapdoor", -1, "waxed_exposed_copper_trapdoor"),
        MC("minecraft:waxed_weathered_copper_trapdoor", -1, "waxed_weathered_copper_trapdoor"),
        MC("minecraft:waxed_oxidized_copper_trapdoor", -1, "waxed_oxidized_copper_trapdoor"),
        // 凝灰岩系列
        MC("minecraft:tuff_stairs", -1, "tuff_stairs"),
        MC("minecraft:tuff_slab", -1, "tuff_slab"),
        MC("minecraft:tuff_wall", -1, "tuff_wall"),
        MC("minecraft:polished_tuff", -1, "polished_tuff"),
        MC("minecraft:polished_tuff_stairs", -1, "polished_tuff_stairs"),
        MC("minecraft:polished_tuff_slab", -1, "polished_tuff_slab"),
        MC("minecraft:polished_tuff_wall", -1, "polished_tuff_wall"),
        MC("minecraft:tuff_bricks", -1, "tuff_bricks"),
        MC("minecraft:tuff_brick_stairs", -1, "tuff_brick_stairs"),
        MC("minecraft:tuff_brick_slab", -1, "tuff_brick_slab"),
        MC("minecraft:tuff_brick_wall", -1, "tuff_brick_wall"),
        MC("minecraft:chiseled_tuff", -1, "chiseled_tuff"),
        MC("minecraft:chiseled_tuff_bricks", -1, "chiseled_tuff_bricks"),

        MC("minecraft:unknown", -1, "unknown"),
    };
    return std::vector<BlockDef>(raw, raw + sizeof(raw) / sizeof(raw[0]));
}
#undef MC
#undef M6

static const std::vector<BlockDef>& Registry() {
    static std::vector<BlockDef> v = BuildRegistry();
    return v;
}

static std::map<int, int>& LegacyMap() {
    static std::map<int, int> m;
    static bool built = false;
    if (!built) {
        const auto& v = Registry();
        for (int i = 0; i < (int)v.size(); ++i)
            if (v[i].legacyId >= 0) m[v[i].legacyId] = i;
        built = true;
    }
    return m;
}

static std::map<std::string, int>& StringMap() {
    static std::map<std::string, int> m;
    static bool built = false;
    if (!built) {
        const auto& v = Registry();
        for (int i = 0; i < (int)v.size(); ++i) {
            m[v[i].key] = i;
            std::string shortKey = v[i].key;
            const char* p = shortKey.c_str();
            const char* colon = strchr(p, ':');
            if (colon) m[std::string(colon + 1)] = i;   // 去掉 "minecraft:" 前缀
        }
        built = true;
    }
    return m;
}

int McBlockCount() { return (int)Registry().size(); }
const BlockDef* McBlockByIndex(int i) {
    const auto& v = Registry();
    return (i >= 0 && i < (int)v.size()) ? &v[i] : nullptr;
}

int McResolveLegacy(int legacyId) {
    auto it = LegacyMap().find(legacyId);
    return it == LegacyMap().end() ? -1 : it->second;
}

int McResolveString(const char* stateId) {
    if (!stateId) return -1;
    std::string s(stateId);
    // 去掉状态后缀 [..]
    size_t br = s.find('[');
    if (br != std::string::npos) s = s.substr(0, br);
    // 去 "minecraft:" 前缀
    const char* p = s.c_str();
    const char* colon = strchr(p, ':');
    if (colon) s = std::string(colon + 1);
    // 小写化
    for (char& c : s) c = (char)::tolower((unsigned char)c);
    auto it = StringMap().find(s);
    return it == StringMap().end() ? -1 : it->second;
}

int McResolve(const std::string& stateId, int legacyId) {
    int r = McResolveString(stateId.c_str());
    if (r >= 0) return r;
    if (legacyId >= 0) return McResolveLegacy(legacyId);
    return -1;
}

// ----------------------------- 预拼图集加载器（核心）-----------------------------

// 解析 .mtl 文件，提取有序材质名列表（按出现顺序 = 图集网格中的排列顺序）
// 返回小写材质名列表；同时记录哪些材质用 RGBA+Alpha（半透明）
static bool ParseMtlFile(const wchar_t* path, std::vector<std::string>& namesOut,
                         std::set<std::string>& transparentOut)
{
    FILE* f = _wfopen(path, L"rb");
    if (!f) return false;

    // 读入全部内容
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 10 * 1024 * 1024) { fclose(f); return false; }   // 安全上限 10MB
    std::vector<char> buf(sz + 1, 0);
    fread(buf.data(), 1, sz, f);
    fclose(f);

    // 逐行解析 newmtl 和 map_d（有 map_d = 半透明）
    const char* p = buf.data();
    std::string curName;
    bool hasAlpha = false;

    while (*p) {
        // 跳过空白行和注释
        while (*p == '\r' || *p == '\n' || *p == ' ' || *p == '\t') ++p;
        if (!*p) break;
        if (*p == '#') { while (*p && *p != '\n') ++p; continue; }

        // newmtl <name>
        if (strncmp(p, "newmtl ", 7) == 0) {
            // 先保存上一个
            if (!curName.empty()) {
                std::string lo = ToLower(curName);
                namesOut.push_back(lo);
                if (hasAlpha) transparentOut.insert(lo);
            }
            p += 7;
            while (*p == ' ' || *p == '\t') ++p;
            const char* start = p;
            while (*p && *p != '\r' && *p != '\n') ++p;
            curName.assign(start, p - start);
            hasAlpha = false;
            continue;
        }

        // map_d → 标记当前材质为半透明
        if (strncmp(p, "map_d ", 6) == 0) {
            hasAlpha = true;
        }

        // 跳到行尾
        while (*p && *p != '\n') ++p;
    }

    // 最后一个
    if (!curName.empty()) {
        std::string lo = ToLower(curName);
        namesOut.push_back(lo);
        if (hasAlpha) transparentOut.insert(lo);
    }

    return !namesOut.empty();
}

// 尝试常见 MC 图集 tile 尺寸，返回能整除图像宽高的最大候选
static int DetectTileSize(int imgW, int imgH, int materialCount) {
    // 按优先级尝试：MC 标准 16px，然后 32, 64...
    int candidates[] = { 16, 32, 64, 8, 128 };
    for (int ts : candidates) {
        if (imgW % ts == 0 && imgH % ts == 0) {
            int cols = imgW / ts, rows = imgH / ts;
            if (cols * rows >= materialCount) return ts;
        }
    }
    // 兜底：从材料数反推（假设接近正方形）
    int side = (int)std::ceil(std::sqrt((double)materialCount));
    if (side > 0 && imgW % side == 0) return imgW / side;
    return 16;   // 最终兜底
}

bool McBuildAtlasFromPrebuilt(const wchar_t* dir, McAtlas& out) {
    if (!dir || !*dir) return false;
    std::wstring base(dir);
    // 确保 base 不以斜杠结尾
    while (!base.empty() && (base.back() == L'\\' || base.back() == L'/'))
        base.pop_back();

    // ---- 1. 解析 MTL 获取有序材质名 ----
    std::wstring mtlPath = base + L"\\ss1.mtl";
    std::vector<std::string> matNames;
    std::set<std::string> transparentMats;
    if (!ParseMtlFile(mtlPath.c_str(), matNames, transparentMats)) return false;

    // 也解析 glass1.mtl（补充玻璃等变体）
    std::wstring glassMtl = base + L"\\glass1.mtl";
    ParseMtlFile(glassMtl.c_str(), matNames, transparentMats);

    int matCount = (int)matNames.size();
    if (matCount == 0) return false;

    // 去重：glass1.mtl 与 ss1.mtl 有同名材质（Grass_Block/Dirt/Cobblestone/Bedrock/Obsidian），
    // 保留首次出现（ss1）的网格位置，避免 glass1 覆盖正确 UV。
    {
        std::set<std::string> seen;
        std::vector<std::string> dedup;
        for (auto& n : matNames) {
            if (seen.insert(n).second) dedup.push_back(n);
        }
        matNames.swap(dedup);
        matCount = (int)matNames.size();
    }
    out.mtlNames = matNames;

    // ---- 2. 加载主图集 PNG ----
    std::wstring rgbPath = base + L"\\ss1-RGB.png";
    int aw = 0, ah = 0;
    if (!WicDecode(rgbPath.c_str(), out.rgba, aw, ah) || aw <= 0 || ah <= 0) return false;
    out.width = aw;
    out.height = ah;

    // ---- 3. 加载 Alpha 遮罩（可选）----
    std::wstring alphaPath = base + L"\\ss1-Alpha.png";
    int aaw = 0, aah = 0;
    WicDecode(alphaPath.c_str(), out.alpha, aaw, aah);   // 失败也继续

    // ---- 4. 计算网格 UV ----
    int tileSz = DetectTileSize(aw, ah, matCount);
    int cols = aw / tileSz;
    // float invW = 1.0f / aw, invH = 1.0f / ah;

    out.rects.clear();
    for (int i = 0; i < matCount; ++i) {
        int cx = (i % cols) * tileSz;
        int cy = (i / cols) * tileSz;
        McAtlasRect r;
        r.u0 = (float)cx / (float)aw;
        r.v0 = (float)cy / (float)ah;
        r.u1 = (float)(cx + tileSz) / (float)aw;
        r.v1 = (float)(cy + tileSz) / (float)ah;
        out.rects[matNames[i]] = r;
    }

    // ---- 5. 基于手工映射表（mc_blockmap.h）建立反向索引：方块身份 → 图集槽位 ----
    // 每个 g_mcBlockMap 条目把其 MTL 名对应的图集矩形，注册到它代表的所有
    // 1.13 短 id（含变体家族 alt）与 1.12 数字 id（data 不区分，同族共用槽位）。
    for (int e = 0; e < g_mcBlockMapCount; ++e) {
        const McBlockMapEntry& en = g_mcBlockMap[e];
        auto it = std::find(matNames.begin(), matNames.end(), ToLower(std::string(en.mtl)));
        if (it == matNames.end()) continue;
        int slot = (int)(it - matNames.begin());

        if (en.id113 && en.id113[0]) {
            std::string k = en.id113;
            const char* c = strchr(k.c_str(), ':'); if (c) k = c + 1;
            for (char& ch : k) ch = (char)::tolower((unsigned char)ch);
            out.keyToSlot[k] = slot;
        }
        if (en.alt && en.alt[0]) {
            std::string s(en.alt); size_t pos = 0;
            while (pos < s.size()) {
                size_t sep = s.find('|', pos);
                std::string a = s.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos);
                pos = (sep == std::string::npos) ? s.size() : sep + 1;
                if (a.empty()) continue;
                const char* c = strchr(a.c_str(), ':'); if (c) a = c + 1;
                for (char& ch : a) ch = (char)::tolower((unsigned char)ch);
                out.keyToSlot[a] = slot;
            }
        }
        if (en.legacyId >= 0) out.legacyToSlot[en.legacyId] = slot;
    }

    // unknown 回退：用最后一个位置或 magenta 色
    if (matCount > 0) {
        int lastIdx = matCount - 1;
        int cx = (lastIdx % cols) * tileSz;
        int cy = (lastIdx / cols) * tileSz;
        out.unknown.u0 = (float)cx / (float)aw;
        out.unknown.v0 = (float)cy / (float)ah;
        out.unknown.u1 = (float)(cx + tileSz) / (float)aw;
        out.unknown.v1 = (float)(cy + tileSz) / (float)ah;
    }

    return true;
}

// ----------------------------- 方块网格 -----------------------------
int McBlockGrid::at(int x, int y, int z) const {
    if (!inRange(x, y, z)) return -1;
    return cells[((size_t)z * (size_t)sy + y) * sx + x];
}
void McBlockGrid::set(int x, int y, int z, int v) {
    if (inRange(x, y, z)) cells[((size_t)z * (size_t)sy + y) * sx + x] = v;
}

void McBuildTestWorld(McBlockGrid& out) {
    const int S = 64, H = 18;   // 4×4 区块（4*16=64），地图默认整张显示即 4×4 区块
    out.sx = S; out.sy = H; out.sz = S;
    out.cells.assign((size_t)S * H * S, -1);
    int bedrock = McResolveString("minecraft:bedrock");
    int stone   = McResolveString("minecraft:stone");
    int dirt    = McResolveString("minecraft:dirt");
    int grass   = McResolveString("minecraft:grass_block");
    int sand    = McResolveString("minecraft:sand");
    int water   = McResolveString("minecraft:water");
    int glass   = McResolveString("minecraft:glass");
    int log     = McResolveString("minecraft:oak_log");
    int leaves  = McResolveString("minecraft:oak_leaves");

    for (int x = 0; x < S; ++x)
        for (int z = 0; z < S; ++z) {
            int top = 6 + (int)(2.0 * sin(x * 0.18) + 2.0 * cos(z * 0.15));  // 降低频率，4×4 区块更平滑
            out.set(x, 0, z, bedrock);
            for (int y = 1; y <= top; ++y) {
                if (y == top) out.set(x, y, z, grass);
                else if (y >= top - 2) out.set(x, y, z, dirt);
                else out.set(x, y, z, stone);
            }
            // 水塘：一个 16×16 区块（1 chunk）
            if (x >= 8 && x < 24 && z >= 8 && z < 24) {
                out.set(x, top, z, sand);
                for (int y = top + 1; y <= top + 2; ++y) out.set(x, y, z, water);
            }
            // 玻璃塔
            if (x == 4 && z == 4)
                for (int y = top + 1; y <= top + 5; ++y) out.set(x, y, z, glass);
        }
    // 中央树
    int cx = S / 2, cz = S / 2, g = 6;
    for (int y = g + 1; y <= g + 4; ++y) out.set(cx, y, cz, log);
    for (int dy = -2; dy <= 1; ++dy)
        for (int dx = -2; dx <= 2; ++dx)
            for (int dz = -2; dz <= 2; ++dz) {
                if (dx == 0 && dz == 0 && dy < 0) continue;
                if (abs(dx) + abs(dz) + abs(dy) > 3) continue;
                int yy = g + 4 + dy;
                if (yy > 0 && yy < H) out.set(cx + dx, yy, cz + dz, leaves);
            }
}

// ----------------------------- 贪心合并网格 -----------------------------

// 方块身份 → 图集矩形（正确处理 1.13 变体家族与 1.12 数字 id；旧回退最后）
// 公开供贪心合并网格与地图管线（mc_map_pipeline）共用。
McAtlasRect McRectFor(const McAtlas& a, int b, int face) {
    const BlockDef* d = McBlockByIndex(b);
    if (d) {
        // ---- 优先：按面查找（tex[MC_TOP]="grass_block_top" 等）----
        // 这是正确的顶视图贴图路径；图集若含独立面纹理会直接命中
        if (face >= 0 && face < MC_FACE_COUNT && d->tex[face] && d->tex[face][0]) {
            std::string stem = ToLower(std::string(d->tex[face]));
            auto it = a.rects.find(stem);
            if (it != a.rects.end()) return it->second;
            // 按面名查 keyToSlot（支持 "grass_block_top" → slot → mtlName → rect）
            auto it2 = a.keyToSlot.find(stem);
            if (it2 != a.keyToSlot.end()) {
                int slot = it2->second;
                if (slot >= 0 && slot < (int)a.mtlNames.size()) {
                    auto rit = a.rects.find(a.mtlNames[slot]);
                    if (rit != a.rects.end()) return rit->second;
                }
            }
        }
        // ---- 回退 1：按方块 key 查找（全方统一纹理，如 stone/dirt）----
        {
            std::string k = d->key;
            const char* c = strchr(k.c_str(), ':'); if (c) k = c + 1;
            for (char& ch : k) ch = (char)::tolower((unsigned char)ch);
            auto it2 = a.keyToSlot.find(k);
            if (it2 != a.keyToSlot.end()) {
                int slot = it2->second;
                if (slot >= 0 && slot < (int)a.mtlNames.size()) {
                    auto rit = a.rects.find(a.mtlNames[slot]);
                    if (rit != a.rects.end()) return rit->second;
                }
            }
        }
        // ---- 回退 2：按 legacyId 查找 ----
        if (d->legacyId >= 0) {
            auto it3 = a.legacyToSlot.find(d->legacyId);
            if (it3 != a.legacyToSlot.end()) {
                int slot = it3->second;
                if (slot >= 0 && slot < (int)a.mtlNames.size()) {
                    auto rit = a.rects.find(a.mtlNames[slot]);
                    if (rit != a.rects.end()) return rit->second;
                }
            }
        }
    }
    if (!d) return a.unknown;
    // ---- 最终兜底：按 key 原名查找 ----
    std::string k = d->key;
    const char* colon = strchr(k.c_str(), ':');
    if (colon) k = colon + 1;
    auto it = a.rects.find(ToLower(k));
    return it != a.rects.end() ? it->second : a.unknown;
}

// ----------------------------- MCA 颜色表（2D 地图用）-----------------------------

// tint 混合：base × tint 逐通道相乘后 >> 8（与 MCA ColorMapping.applyTint 一致）
static inline uint32_t McApplyTint(uint32_t base, uint32_t tint) {
    const uint32_t nr = ((tint >> 16 & 0xFFu) * (base >> 16 & 0xFFu)) >> 8;
    const uint32_t ng = ((tint >> 8  & 0xFFu) * (base >> 8  & 0xFFu)) >> 8;
    const uint32_t nb = ((tint       & 0xFFu) * (base       & 0xFFu)) >> 8;
    return (nr << 16) | (ng << 8) | nb;
}

bool McBlockIsTransparent(int blockIndex) {
    const BlockDef* d = McBlockByIndex(blockIndex);
    return d && McIsTransparent(d->key);
}

uint32_t McBlockColor(int blockIndex) {
    // 惰性构建：注册表索引 → 颜色缓存（含 tint），此后 O(1)
    static std::vector<uint32_t> cache;
    const int n = McBlockCount();
    if ((int)cache.size() != n) {
        cache.assign((size_t)n, 0x808080);
        const int lo = 0, hi = (int)(sizeof(kMcColors) / sizeof(kMcColors[0])) - 1;
        for (int i = 0; i < n; ++i) {
            const BlockDef* d = McBlockByIndex(i);
            if (!d) continue;
            const char* key = d->key;   // "minecraft:xxx"
            const McColorEntry* e = McColorFind(key, lo, hi);
            uint32_t rgb = e ? e->rgb : 0x808080;
            if (McIsGrassTint(key))       rgb = McApplyTint(rgb, kMcTintGrass);
            else if (McIsFoliageTint(key)) rgb = McApplyTint(rgb, kMcTintFoliage);
            else if (McIsWaterTint(key))  rgb = McApplyTint(rgb, kMcTintWater);
            cache[(size_t)i] = rgb;
        }
    }
    return blockIndex >= 0 && blockIndex < (int)cache.size() ? cache[(size_t)blockIndex] : 0x101010;
}

void McBuildMergedMesh(const McBlockGrid& grid, const McAtlas& atlas, SceneObject& out,
                       bool useMca) {
    out.solidVerts.clear();
    out.solidIndices.clear();
    out.mcUv.clear();
    out.wireVerts.clear();
    out.wireIndices.clear();

    auto get = [&](int x, int y, int z) -> int {
        return grid.inRange(x, y, z) ? grid.at(x, y, z) : -1;
    };
    auto isSolid = [&](int v) -> bool { return v >= 0; };

    // 查找 UV：使用自由函数 McRectFor（正确处理 1.13 变体家族与 1.12 数字 id）
    auto rectFor = [&](int b, int face) -> McAtlasRect { return McRectFor(atlas, b, face); };

    const int dim[3] = { grid.sx, grid.sy, grid.sz };

    // 在 d 轴上处理：u=(d+1)%3, v=(d+2)%3
    for (int d = 0; d < 3; ++d) {
        int u = (d + 1) % 3, v = (d + 2) % 3;
        int Sd = dim[d], Su = dim[u], Sv = dim[v];
        std::vector<int> mask(Su * Sv, -1);
        std::vector<char> maskFace(Su * Sv, 0);

        for (int dpos = 0; dpos < Sd; ++dpos) {
            for (int c = 0; c < Sv; ++c)
                for (int b = 0; b < Su; ++b) {
                        int pa[3] = {0,0,0}; pa[d]=dpos; pa[u]=b; pa[v]=c;
                        int A2 = get(pa[0], pa[1], pa[2]);
                        pa[d] = dpos + 1;
                        int B2 = get(pa[0], pa[1], pa[2]);
                        int m = -1; int face = 0;
                        if (isSolid(A2) && !isSolid(B2)) {
                            m = A2; face = (d==0?MC_EAST : d==1?MC_TOP : MC_SOUTH);
                        } else if (isSolid(B2) && !isSolid(A2)) {
                            m = B2; face = (d==0?MC_WEST : d==1?MC_BOTTOM : MC_NORTH);
                        }
                        mask[c * Su + b] = m; maskFace[c * Su + b] = (char)face;
                    }

            // 贪心合并
            for (int c = 0; c < Sv; ++c)
                for (int b = 0; b < Su; ++b) {
                    int m = mask[c * Su + b];
                    if (m < 0) continue;
                    int face = maskFace[c * Su + b];
                    int w = 1;
                    while (b + w < Su && mask[c * Su + (b + w)] == m && maskFace[c * Su + (b + w)] == face) ++w;
                    int h = 1; bool ok = true;
                    while (c + h < Sv) {
                        for (int k = 0; k < w; ++k) {
                            int idx = (c + h) * Su + (b + k);
                            if (mask[idx] != m || maskFace[idx] != face) { ok = false; break; }
                        }
                        if (!ok) break;
                        ++h;
                    }
                    // 发射合并后的面
                    int planeD = (face == MC_EAST || face == MC_TOP || face == MC_SOUTH) ? dpos + 1 : dpos;
                    float nrm[3] = {0,0,0}; nrm[d] = (face==MC_EAST||face==MC_TOP||face==MC_SOUTH) ? 1.0f : -1.0f;
                    // 渲染模式（接口参数 useMca）：
                    //   MCA 模式：顶点色=方块色（MCA 颜色表），uv 全部指向 1×1 白图集 → 纯色显示
                    //   纹理模式：顶点色全白，uv 指向 ss1 图集矩形 → 纹理显示
                    float col[3] = { 1.0f, 1.0f, 1.0f };
                    float uu[4] = { 0, 0, 0, 0 }, vv[4] = { 0, 0, 0, 0 };
                    if (useMca) {
                        const uint32_t rgb = McBlockColor(m);
                        col[0] = (float)(rgb >> 16) / 255.0f;
                        col[1] = (float)(rgb >> 8 & 0xFF) / 255.0f;
                        col[2] = (float)(rgb & 0xFF) / 255.0f;
                    } else {
                        McAtlasRect r = rectFor(m, face);
                        uu[0] = r.u0; uu[1] = r.u1; uu[2] = r.u1; uu[3] = r.u0;
                        vv[0] = r.v0; vv[1] = r.v0; vv[2] = r.v1; vv[3] = r.v0;
                    }
                    int cu[4] = { b, b + w, b + w, b };
                    int cv[4] = { c, c, c + h, c + h };
                    int base = (int)out.solidVerts.size();
                    for (int i = 0; i < 4; ++i) {
                        int pp[3] = {0,0,0}; pp[d]=planeD; pp[u]=cu[i]; pp[v]=cv[i];
                        VertexSolid vs;
                        vs.pos[0]= (float)pp[0]; vs.pos[1]= (float)pp[1]; vs.pos[2]= (float)pp[2];
                        vs.normal[0]=nrm[0]; vs.normal[1]=nrm[1]; vs.normal[2]=nrm[2];
                        vs.color[0]=col[0]; vs.color[1]=col[1]; vs.color[2]=col[2]; vs.color[3]=1.0f;
                        out.solidVerts.push_back(vs);
                        out.mcUv.push_back(uu[i]); out.mcUv.push_back(vv[i]);
                    }
                    out.solidIndices.push_back(base + 0);
                    out.solidIndices.push_back(base + 1);
                    out.solidIndices.push_back(base + 2);
                    out.solidIndices.push_back(base + 0);
                    out.solidIndices.push_back(base + 2);
                    out.solidIndices.push_back(base + 3);
                    // 清除已合并
                    for (int j = 0; j < h; ++j)
                        for (int i = 0; i < w; ++i) mask[(c + j) * Su + (b + i)] = -1;
                }
        }
    }

    // 图集由本函数按 useMca 设置：
    //   MCA 模式：1×1 白色（顶点色=方块色，采样白色即纯色显示）
    //   纹理模式：ss1 图集图像
    if (useMca) {
        out.texRgba.assign({ 255, 255, 255, 255 });
        out.texWidth = 1;
        out.texHeight = 1;
    } else {
        out.texRgba = atlas.rgba;
        out.texWidth = atlas.width;
        out.texHeight = atlas.height;
    }
}


bool McBuildDemoWorld(SceneObject& out, McBlockGrid* outGrid, McAtlas* outAtlas) {
    McBlockGrid g;
    McBuildTestWorld(g);
    McAtlas a;

    // 从预拼图集目录加载（优先 exe 同级 资源/s1 → 其次环境变量）
    wchar_t exeDir[MAX_PATH] = {0};
    bool loaded = false;
    if (GetModuleFileNameW(nullptr, exeDir, MAX_PATH)) {
        std::wstring d(exeDir);
        size_t sl = d.find_last_of(L"\\/");
        if (sl != std::wstring::npos) d = d.substr(0, sl);
        std::wstring prebuilt = d + L"\\资源\\s1";
        loaded = McBuildAtlasFromPrebuilt(prebuilt.c_str(), a);
    }
    if (!loaded) {
        wchar_t env[512] = {0};
        if (GetEnvironmentVariableW(L"AWA_MC_ATLAS", env, 512) && env[0])
            loaded = McBuildAtlasFromPrebuilt(env, a);
    }
    // 即使图集加载失败也继续构建几何（uv 会回退到 unknown）

    McBuildMergedMesh(g, a, out);   // 默认 useMca=true（MCA 颜色渲染；图集由该函数按模式设置）
    out.name = L"我的世界（合并网格演示）";
    out.tx = out.ty = out.tz = 0;
    if (outGrid) *outGrid = g;
    if (outAtlas) *outAtlas = a;
    return true;
}
