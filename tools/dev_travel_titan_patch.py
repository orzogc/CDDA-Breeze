from pathlib import Path
import json

ROOT = Path('.')

def replace_once(path, old, new):
    p = ROOT / path
    text = p.read_text(encoding='utf-8')
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f'{path}: expected one match, got {count}: {old[:80]!r}')
    p.write_text(text.replace(old, new, 1), encoding='utf-8')

# 1. 通用枪械力量伤害参数。
replace_once('src/itype.h',
'''    int ammo_to_fire = 1;\n};''',
'''    int ammo_to_fire = 1;\n\n    // Optional strength scaling for the gun's base ranged damage.\n    int strength_damage_reference = -1;\n    float strength_damage_per_point = 0.0f;\n    damage_type strength_damage_type = damage_type::NONE;\n};''')

replace_once('src/item_factory.cpp',
'''    assign( jo, "ammo_to_fire", slot.ammo_to_fire, strict, 1 );\n\n    if( jo.has_array( "valid_mod_locations" ) ) {''',
'''    assign( jo, "ammo_to_fire", slot.ammo_to_fire, strict, 1 );\n    assign( jo, "strength_damage_reference", slot.strength_damage_reference, strict, -1 );\n    assign( jo, "strength_damage_per_point", slot.strength_damage_per_point, strict, 0.0f );\n    if( jo.has_string( "strength_damage_type" ) ) {\n        slot.strength_damage_type = io::string_to_enum<damage_type>( jo.get_string( "strength_damage_type" ) );\n    }\n\n    if( jo.has_array( "valid_mod_locations" ) ) {''')

replace_once('src/item.h',
'''        damage_instance gun_damage( bool with_ammo = true, bool shot = false ) const;''',
'''        damage_instance gun_damage( bool with_ammo = true, bool shot = false,\n                                    const Character *shooter = nullptr ) const;''')

replace_once('src/item.cpp',
'''damage_instance item::gun_damage( bool with_ammo, bool shot ) const\n{\n    if( !is_gun() ) {\n        return damage_instance();\n    }\n    damage_instance ret = type->gun->damage;\n''',
'''damage_instance item::gun_damage( bool with_ammo, bool shot, const Character *shooter ) const\n{\n    if( !is_gun() ) {\n        return damage_instance();\n    }\n    damage_instance ret = type->gun->damage;\n\n    if( shooter != nullptr && type->gun->strength_damage_reference >= 0 &&\n        type->gun->strength_damage_per_point != 0.0f &&\n        type->gun->strength_damage_type != damage_type::NONE ) {\n        const float bonus = ( shooter->get_str() - type->gun->strength_damage_reference ) *\n                            type->gun->strength_damage_per_point;\n        bool adjusted = false;\n        for( damage_unit &du : ret ) {\n            if( du.type == type->gun->strength_damage_type ) {\n                du.amount = std::max( 0.0f, du.amount + bonus );\n                adjusted = true;\n                break;\n            }\n        }\n        if( !adjusted && bonus > 0.0f ) {\n            ret.add_damage( type->gun->strength_damage_type, bonus );\n        }\n    }\n''')

# 2. 真实射击支持“必中”，并把射手传给伤害计算。
replace_once('src/character.h',
'''        int fire_gun( const tripoint &target, int shots, item &gun );''',
'''        int fire_gun( const tripoint &target, int shots, item &gun, bool guaranteed_hit = false );''')

replace_once('src/ranged.cpp',
'''static projectile make_gun_projectile( const item &gun );''',
'''static projectile make_gun_projectile( const item &gun, const Character &shooter );''')

replace_once('src/ranged.cpp',
'''int Character::fire_gun( const tripoint &target, int shots, item &gun )\n{''',
'''int Character::fire_gun( const tripoint &target, int shots, item &gun, bool guaranteed_hit )\n{''')

replace_once('src/ranged.cpp',
'''        projectile proj = make_gun_projectile( gun );\n        dispersion_sources dispersion = get_weapon_dispersion( gun );\n        dispersion.add_range( recoil_total() );\n        dispersion.add_spread( proj.shot_spread );''',
'''        projectile proj = make_gun_projectile( gun, *this );\n        dispersion_sources dispersion = guaranteed_hit ? dispersion_sources{ 0 } : get_weapon_dispersion( gun );\n        if( !guaranteed_hit ) {\n            dispersion.add_range( recoil_total() );\n            dispersion.add_spread( proj.shot_spread );\n        }''')

replace_once('src/ranged.cpp',
'''static projectile make_gun_projectile( const item &gun )\n{\n    projectile proj;\n    proj.speed  = 1000;\n    proj.impact = gun.gun_damage();\n    proj.shot_impact = gun.gun_damage( true, true );''',
'''static projectile make_gun_projectile( const item &gun, const Character &shooter )\n{\n    projectile proj;\n    proj.speed  = 1000;\n    proj.impact = gun.gun_damage( true, false, &shooter );\n    proj.shot_impact = gun.gun_damage( true, true, &shooter );''')

# 3. 新法术效果：装填一发真实弹药并调用真实射击。
replace_once('src/magic.h',
'''void attack( const spell &sp, Creature &caster,\n             const tripoint &epicenter );\nvoid targeted_polymorph''',
'''void attack( const spell &sp, Creature &caster,\n             const tripoint &epicenter );\nvoid fire_gun( const spell &sp, Creature &caster, const tripoint &target );\nvoid targeted_polymorph''')

replace_once('src/magic.h',
'''    { "attack", spell_effect::attack },\n    { "targeted_polymorph",''',
'''    { "attack", spell_effect::attack },\n    { "fire_gun", spell_effect::fire_gun },\n    { "targeted_polymorph",''')

replace_once('src/magic_spell_effect.cpp',
'''static const species_id species_SLIME( "SLIME" );\n\nstatic const trait_id trait_KILLER''',
'''static const species_id species_SLIME( "SLIME" );\n\nstatic const skill_id skill_archery( "archery" );\n\nstatic const trait_id trait_KILLER''')

replace_once('src/magic_spell_effect.cpp',
'''void spell_effect::attack( const spell &sp, Creature &caster,\n                           const tripoint &epicenter )\n{\n    damage_targets( sp, caster, spell_effect_area( sp, epicenter, caster ) );\n    if( sp.has_flag( spell_flag::SWAP_POS ) ) {\n        swap_pos( caster, epicenter );\n    }\n}\n''',
'''void spell_effect::attack( const spell &sp, Creature &caster,\n                           const tripoint &epicenter )\n{\n    damage_targets( sp, caster, spell_effect_area( sp, epicenter, caster ) );\n    if( sp.has_flag( spell_flag::SWAP_POS ) ) {\n        swap_pos( caster, epicenter );\n    }\n}\n\nvoid spell_effect::fire_gun( const spell &sp, Creature &caster, const tripoint &target )\n{\n    Character *you = caster.as_character();\n    if( you == nullptr ) {\n        return;\n    }\n\n    item_location weapon = you->get_wielded_item();\n    if( !weapon || !weapon->is_gun() ) {\n        you->add_msg_if_player( m_bad, _( "You must wield a ranged weapon to use this spell." ) );\n        return;\n    }\n\n    const std::string data = sp.effect_data();\n    static const std::string flag_prefix = "REQUIRE_FLAG:";\n    const size_t flag_pos = data.find( flag_prefix );\n    if( flag_pos != std::string::npos ) {\n        const size_t flag_begin = flag_pos + flag_prefix.size();\n        const size_t flag_end = data.find( ';', flag_begin );\n        const std::string flag_name = data.substr( flag_begin, flag_end - flag_begin );\n        if( flag_name.empty() || !weapon->has_flag( flag_id( flag_name ) ) ) {\n            you->add_msg_if_player( m_bad, _( "You are not wielding the weapon required by this spell." ) );\n            return;\n        }\n    }\n\n    const int required = weapon->ammo_required();\n    if( required > 0 && weapon->ammo_remaining() < required ) {\n        std::vector<item_location> ammo = you->find_ammo( *weapon, true, -1 );\n        if( ammo.empty() || !weapon->reload( *you, ammo.front(), required ) ) {\n            you->add_msg_if_player( m_bad, _( "You have no compatible ammunition for your %s." ),\n                                    weapon->tname() );\n            return;\n        }\n    }\n\n    const int shots = you->fire_gun( target, 1, *weapon, true );\n    if( shots <= 0 || data.find( "ARCHERY_STAMINA" ) == std::string::npos ||\n        weapon->gun_skill() != skill_archery ) {\n        return;\n    }\n\n    // Match the normal archery draw cost, then scale Flourish from 2x at level 0 to 1x at max level.\n    const int archery_skill = you->get_skill_level( skill_archery );\n    const int athletics_skill = you->get_skill_level( skill_archery );\n    const int skill_modifier = ( 2 * archery_skill + athletics_skill ) / 3;\n    const int normal_cost = static_cast<int>( std::pow( 20 - skill_modifier, 2 ) );\n    const int max_level = std::max( 1, sp.get_max_level() );\n    const double mastery = std::clamp( static_cast<double>( sp.get_level() ) / max_level, 0.0, 1.0 );\n    const int stamina_cost = static_cast<int>( std::lround( normal_cost * ( 2.0 - mastery ) ) );\n    you->mod_stamina( -stamina_cost );\n}\n''')

# 4. 旅行的意义：泰坦弦弓、华舞与玛雅介绍。
mod_path = ROOT / 'data/mods/旅行的意义/角色列表/玛雅·斯卡利特/02_弓术与巴里委托.json'
data = json.loads(mod_path.read_text(encoding='utf-8'))

def obj(obj_id):
    for entry in data:
        if entry.get('id') == obj_id:
            return entry
    raise RuntimeError(f'missing object: {obj_id}')

flag = obj('TRAVEL_TITAN_STRING_BOW')
flag['info'] = '泰坦弦弓。力量会直接改变弓身伤害，可作为挥砍华舞的施法工具。'

bow = obj('travel_titan_string_bow')
bow['name'] = {'str': '泰坦弦弓'}
bow['description'] = ('一把黑金色复合弓，弓臂上的金纹会随拉弦者的力量一同绷紧。它没有固定的拉力档位，真正决定箭势的是持弓人的力量。力量8时弓身提供60点基础远程伤害，每高于8点力量1点增加2点伤害，每低于8点力量1点减少2点伤害。\n\n'
                      '人们常以为天生的弓手都该身形修长、手臂纤细——这完全错了。把弓弦一路拉到颧骨需要多年的训练；到最后，你的手臂会像钢铁一样坚硬。')
bow['min_strength'] = 1
bow['use_action'] = []
bow['ranged_damage'] = [{'damage_type': 'stab', 'amount': 60}]
bow['strength_damage_reference'] = 8
bow['strength_damage_per_point'] = 2
bow['strength_damage_type'] = 'stab'
delete = bow.setdefault('delete', {})
flags = delete.setdefault('flags', [])
if 'STR_DRAW' not in flags:
    flags.append('STR_DRAW')

# 旧存档中的中、高张力形态迁回唯一的泰坦弦弓。
remove_ids = {'travel_titan_string_bow_medium', 'travel_titan_string_bow_high'}
data = [entry for entry in data if entry.get('id') not in remove_ids]
base_index = next(i for i, entry in enumerate(data) if entry.get('id') == 'travel_titan_string_bow')
data[base_index + 1:base_index + 1] = [
    {'type': 'MIGRATION', 'id': 'travel_titan_string_bow_medium', 'replace': 'travel_titan_string_bow'},
    {'type': 'MIGRATION', 'id': 'travel_titan_string_bow_high', 'replace': 'travel_titan_string_bow'},
]

req = obj('spell_components_travel_titan_dance')
req['//'] = '挥砍华舞要求玩家持有并实际手持泰坦弦弓。箭矢由真实射击流程装填与消耗，不再作为法术材料销毁。'
req['tools'] = [[['travel_titan_string_bow', -1]]]
req.pop('components', None)

main = obj('SPELL_TRAVEL_TITAN_STRING_DANCE')
main['description'] = {'str': '一段为泰坦弦弓改写的战场乐句。你会连续放出两支真正的箭，第一箭离弦后可以重新选择第二个目标。两箭都直接使用泰坦弦弓、所装箭矢与弹道系统的实际伤害和命中效果，箭矢也遵循原本的损坏与回收规则。初学时一次华舞消耗约为一次普通射击两倍的耐力，熟练后会逐渐降到与一次普通射击相同。\n\n别追着箭走。让箭追上你的旋律。'}
main['effect'] = 'fire_gun'
main['effect_str'] = 'REQUIRE_FLAG:TRAVEL_TITAN_STRING_BOW;ARCHERY_STAMINA'
main['energy_source'] = 'NONE'
main['base_energy_cost'] = 0
main['final_energy_cost'] = 0
main['energy_increment'] = 0
main['min_damage'] = 0
main['max_damage'] = 0
main['damage_increment'] = 0
main.pop('damage_type', None)

echo = obj('SPELL_TRAVEL_TITAN_FLOURISH_ECHO')
echo['description'] = {'str': '挥砍华舞的第二次真实射击。'}
echo['effect'] = 'fire_gun'
echo['effect_str'] = 'REQUIRE_FLAG:TRAVEL_TITAN_STRING_BOW'
echo['min_damage'] = 0
echo['max_damage'] = 0
echo['damage_increment'] = 0
echo.pop('damage_type', None)

second = obj('EOC_TRAVEL_TITAN_FLOURISH_SECOND_TARGET')
second['//'] = '第一箭由真实射击流程结算后重新打开敌对目标瞄准。第二箭同样使用真实箭矢与弹道；取消瞄准则不追加第二箭。'
second['effect'] = [
    {'u_message': '第一箭离弦，华舞的第二拍已经接上。选择第二个目标。', 'type': 'info'},
    {'u_cast_spell': {'id': 'SPELL_TRAVEL_TITAN_FLOURISH_ECHO', 'min_level': 0, 'max_level': 0}, 'targeted': True},
]

bow_topic = obj('TALK_TRAVEL_MAYA_BARRY_REWARD_BOW')
bow_topic['dynamic_line'] = ('长盒里躺着一把黑金色复合弓。\n\n“泰坦弦弓。”玛雅说，“名字来自灾变前我喜欢的一场游戏——一个不肯向血统和命运低头的吟游诗人。很夸张，但叫久了就改不掉了。”\n\n'
                             '她抬起弓臂，用拇指沿着金纹慢慢划过去。\n\n“别找什么低、中、高张力开关，它现在只认你自己有多少力气。八点力量是基准，弓身六十点伤害；往上每多一点力量，就再多两点，八点以下也一样往回减。没有最低力量门槛，拉得动就能射。”\n\n'
                             '她又点了点两本薄册。\n\n“这本弓箭入门手册是灾变前留下的。另一本才是我整理的华舞笔记。学会以后，一次起弦里是真的放两支箭，第一箭之后你还能换第二个目标。箭头原本的穿甲、爆炸和回收都照算，不是烧掉一支箭换两道光。刚学时会比普通一箭累一倍，练熟以后，耐力消耗会压到差不多一箭。”')

mod_path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

# 5. 玛雅维护说明同步。
info_path = ROOT / 'data/mods/旅行的意义/角色列表/玛雅·斯卡利特/说明.txt'
info = info_path.read_text(encoding='utf-8')
old = '完成巴里救援后获得泰坦弦弓、大箭筒、金属宽刃箭与《吟游诗人的乐章》。乐章使用 Breeze 原生 learn_spell 流程研习“挥砍华舞”。弦弓按低→中→高→低循环，力量需求为8/12/16；一次华舞消耗1支兼容箭和一次耐力，第一箭后重新选择第二目标，两箭可攻击相同或不同目标。经验由本体法术系统处理。音效映射位于“音效/泰坦弦弓_CC-Sounds.json”。'
new = '完成巴里救援后获得泰坦弦弓、大箭筒、金属宽刃箭与《吟游诗人的乐章》。乐章使用 Breeze 原生 learn_spell 流程研习“挥砍华舞”。泰坦弦弓取消低、中、高三档张力，不再设置实际力量门槛；力量8时弓身基础伤害为60，每高于8点力量1点增加2点，每低于8点力量1点减少2点。挥砍华舞改为连续两次真实射击，第一箭后重新选择第二目标，两箭均强制零散布并完整使用当前箭矢的伤害、特殊效果、损坏与回收流程。华舞初始耐力消耗为一次普通射击的2倍，随法术等级线性下降，满级与一次普通射击相同。经验仍由本体法术系统处理。音效映射位于“音效/泰坦弦弓_CC-Sounds.json”。'
if info.count(old) != 1:
    raise RuntimeError('玛雅说明中的旧泰坦弦弓段落未唯一匹配')
info_path.write_text(info.replace(old, new, 1), encoding='utf-8')

# 6. 音效只保留唯一弓形态。
sound_path = ROOT / 'data/mods/旅行的意义/音效/泰坦弦弓_CC-Sounds.json'
sounds = json.loads(sound_path.read_text(encoding='utf-8'))
sounds = [entry for entry in sounds if entry.get('variant') not in remove_ids]
sound_path.write_text(json.dumps(sounds, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

# 7. 来生酒吧大地图派系标签，只标记酒吧主格，避免两格据点重复叠字。
label_path = ROOT / 'data/mods/旅行的意义/地图派系/公路酒馆/07_大地图派系标签.json'
label_path.write_text(json.dumps([
    {'type': 'faction_camp_label', 'overmap_terrain': 'travel_roadside_tavern', 'name': '来生酒吧'}
], ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

# 基础自检。
json.loads(mod_path.read_text(encoding='utf-8'))
json.loads(sound_path.read_text(encoding='utf-8'))
json.loads(label_path.read_text(encoding='utf-8'))

for old_id in sorted(remove_ids):
    matches = [entry for entry in data if entry.get('id') == old_id]
    if len(matches) != 1 or matches[0].get('type') != 'MIGRATION':
        raise RuntimeError(f'{old_id} migration invalid')

print('旅行的意义：泰坦弦弓与来生酒吧补丁已应用。')
