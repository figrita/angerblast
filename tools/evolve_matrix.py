#!/usr/bin/env python3
"""Inject a unique per-cell mechanical fingerprint into every weapon x trifecta
evolution in assets/evolutions.toml. Column [mods.*] stay as the base; each cell
gets 1-3 override knobs expressing how THAT trifecta lands on THAT weapon."""

import re
from pathlib import Path

W = ['MAINGUN','SIDEGUN','TAILGUN','HOMING','LIGHTNING','AURA','ORBITAL',
     'AIMED','AUTOAIM','MISSILE','FLAME','GOO','ION']

# OVR[tri][weapon] -> dict of override keys
OVR = {
 'inferno': {
  'MAINGUN':{'pierce_add':2},'SIDEGUN':{'multi_add':1},'TAILGUN':{'rate_mul':0.75},
  'HOMING':{'on_kill':'frag'},'LIGHTNING':{'on_hit_effect':'shock'},'AURA':{'size_mul':1.7},
  'ORBITAL':{'damage_mul':2.6},'AIMED':{'rate_mul':0.85},'AUTOAIM':{'damage_mul':2.2},
  'MISSILE':{'multi_add':2},'FLAME':{'on_hit_effect':'burn','on_hit_mag':10},
  'GOO':{'on_hit_effect':'goo'},'ION':{'size_mul':2.0}},
 'stream': {
  'MAINGUN':{'pierce_add':5},'SIDEGUN':{'size_mul':1.3},'TAILGUN':{'damage_mul':1.7},
  'HOMING':{'rate_mul':0.7},'LIGHTNING':{'multi_add':1},'AURA':{'size_mul':1.4},
  'ORBITAL':{'on_hit_mag':0.5},'AIMED':{'damage_mul':1.8},'AUTOAIM':{'pierce_add':4},
  'MISSILE':{'rate_mul':0.7},'FLAME':{'pierce_add':6},
  'GOO':{'on_hit_mag':0.5},'ION':{'pierce_add':99,'damage_mul':1.6}},
 'swarm': {
  'MAINGUN':{'multi_add':5},'SIDEGUN':{'multi_add':6},'TAILGUN':{'multi_add':5},
  'HOMING':{'multi_add':7},'LIGHTNING':{'multi_add':2},'AURA':{'size_mul':1.3},
  'ORBITAL':{'multi_add':5},'AIMED':{'multi_add':4},'AUTOAIM':{'multi_add':5},
  'MISSILE':{'multi_add':6},'FLAME':{'multi_add':8},'GOO':{'multi_add':5},
  'ION':{'multi_add':3}},
 'blitz': {
  'MAINGUN':{'rate_mul':0.38},'SIDEGUN':{'rate_mul':0.40},'TAILGUN':{'rate_mul':0.35},
  'HOMING':{'rate_mul':0.5,'multi_add':1},'LIGHTNING':{'rate_mul':0.42},'AURA':{'rate_mul':0.4},
  'ORBITAL':{'damage_mul':1.5,'size_mul':1.2},'AIMED':{'rate_mul':0.4},'AUTOAIM':{'rate_mul':0.42},
  'MISSILE':{'rate_mul':0.45},'FLAME':{'rate_mul':0.4,'on_hit_effect':'burn','on_hit_mag':6},
  'GOO':{'rate_mul':0.5},'ION':{'rate_mul':0.5}},
 'lance': {
  'MAINGUN':{'damage_mul':1.8},'SIDEGUN':{'size_mul':1.2},'TAILGUN':{'damage_mul':1.7},
  'HOMING':{'rate_mul':0.8},'LIGHTNING':{'multi_add':1},'AURA':{'size_mul':1.5},
  'ORBITAL':{'damage_mul':1.9},'AIMED':{'damage_mul':1.8},'AUTOAIM':{'rate_mul':0.85},
  'MISSILE':{'damage_mul':1.8},'FLAME':{'size_mul':1.3},'GOO':{'size_mul':1.4},
  'ION':{'size_mul':1.8}},
 'napalm': {
  'MAINGUN':{'on_hit_mag':16},'SIDEGUN':{'multi_add':1},'TAILGUN':{'rate_mul':0.85},
  'HOMING':{'on_hit_mag':18},'LIGHTNING':{'on_hit_mag':16},'AURA':{'aura_effect':'burn','on_hit_mag':12},
  'ORBITAL':{'aura_effect':'burn','on_hit_mag':10},'AIMED':{'on_hit_mag':16},'AUTOAIM':{'on_hit_mag':15},
  'MISSILE':{'on_hit_mag':20},'FLAME':{'on_hit_mag':22},'GOO':{'on_hit_mag':18,'size_mul':1.4},
  'ION':{'on_hit_mag':20}},
 'scatter': {
  'MAINGUN':{'multi_add':6},'SIDEGUN':{'multi_add':7},'TAILGUN':{'multi_add':6},
  'HOMING':{'multi_add':5},'LIGHTNING':{'multi_add':3},'AURA':{'on_kill':'frag'},
  'ORBITAL':{'on_kill':'frag'},'AIMED':{'multi_add':6},'AUTOAIM':{'multi_add':5},
  'MISSILE':{'multi_add':5},'FLAME':{'multi_add':9},'GOO':{'multi_add':6},
  'ION':{'multi_add':4}},
 'railgun': {
  'MAINGUN':{'damage_mul':3.4},'SIDEGUN':{'size_mul':1.8},'TAILGUN':{'damage_mul':3.2},
  'HOMING':{'damage_mul':3.6,'rate_mul':1.5},'LIGHTNING':{'damage_mul':3.4},'AURA':{'damage_mul':2.6,'size_mul':2.0},
  'ORBITAL':{'damage_mul':3.2,'size_mul':2.2},'AIMED':{'damage_mul':3.4},'AUTOAIM':{'rate_mul':1.2},
  'MISSILE':{'damage_mul':4.0,'rate_mul':1.6},'FLAME':{'damage_mul':2.8},'GOO':{'size_mul':2.0},
  'ION':{'damage_mul':4.2,'rate_mul':1.5}},
 'tempest': {
  'MAINGUN':{'rate_mul':0.5},'SIDEGUN':{'multi_add':3},'TAILGUN':{'rate_mul':0.5},
  'HOMING':{'multi_add':3},'LIGHTNING':{'multi_add':2},'AURA':{'rate_mul':0.5},
  'ORBITAL':{'multi_add':3},'AIMED':{'rate_mul':0.5},'AUTOAIM':{'rate_mul':0.52},
  'MISSILE':{'multi_add':3},'FLAME':{'rate_mul':0.45},'GOO':{'multi_add':3},
  'ION':{'rate_mul':0.6}},
 'void': {
  'MAINGUN':{'pierce_add':6},'SIDEGUN':{'on_hit_mag':0.5},'TAILGUN':{'damage_mul':2.4},
  'HOMING':{'on_hit_effect':'mark'},'LIGHTNING':{'on_hit_mag':0.5},'AURA':{'aura_effect':'weaken','on_hit_mag':0.35},
  'ORBITAL':{'damage_mul':2.5},'AIMED':{'pierce_add':6},'AUTOAIM':{'damage_mul':2.4},
  'MISSILE':{'size_mul':1.6},'FLAME':{'on_hit_mag':0.5},'GOO':{'on_hit_mag':0.55},
  'ION':{'damage_mul':2.6}},
 'prism': {
  'MAINGUN':{'multi_add':3},'SIDEGUN':{'multi_add':3},'TAILGUN':{'damage_mul':1.7},
  'HOMING':{'multi_add':3},'LIGHTNING':{'multi_add':2},'AURA':{'size_mul':1.3},
  'ORBITAL':{'multi_add':3},'AIMED':{'damage_mul':1.7},'AUTOAIM':{'rate_mul':0.8},
  'MISSILE':{'damage_mul':1.7},'FLAME':{'multi_add':3},'GOO':{'size_mul':1.2},
  'ION':{'damage_mul':1.7}},
 'chaos': {
  'MAINGUN':{'on_hit_mag':8},'SIDEGUN':{'multi_add':4},'TAILGUN':{'rate_mul':0.7},
  'HOMING':{'multi_add':4},'LIGHTNING':{'on_hit_mag':8},'AURA':{'aura_effect':'poison','on_hit_mag':5},
  'ORBITAL':{'multi_add':4},'AIMED':{'multi_add':3,'on_hit_mag':7},'AUTOAIM':{'rate_mul':0.75},
  'MISSILE':{'multi_add':4},'FLAME':{'on_hit_mag':9},'GOO':{'on_hit_mag':9},
  'ION':{'multi_add':3,'damage_mul':1.9}},
}
TRI = ['inferno','stream','swarm','blitz','lance','napalm','scatter','railgun',
       'tempest','void','prism','chaos']

src = Path('assets/evolutions.toml').read_text()
lines = src.splitlines()
out = []
wp_i = -1
cell_i = 0
for ln in lines:
    m = re.match(r'\s*\{name="([^"]+)", desc="([^"]+)"\},', ln)
    if re.match(r'\[\[row\]\]', ln):
        wp_i += 1; cell_i = 0
        out.append(ln); continue
    if m and wp_i >= 0:
        tri = TRI[cell_i]
        ov = OVR[tri][W[wp_i]]
        extras = []
        for k, v in ov.items():
            extras.append(f'{k}="{v}"' if isinstance(v, str) else f'{k}={v}')
        out.append(f'  {{name="{m.group(1)}", desc="{m.group(2)}", ' + ', '.join(extras) + '},')
        cell_i += 1
        continue
    out.append(ln)
Path('assets/evolutions.toml').write_text('\n'.join(out) + '\n')
assert wp_i == 12 and cell_i == 12, (wp_i, cell_i)
print(f'156 cells fingerprinted ({wp_i+1} weapons x 12 trifectas)')
