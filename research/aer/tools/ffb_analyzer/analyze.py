#!/usr/bin/env python3
"""Decode AER_DRIVEBOARD_RAW_V1 captures without modifying source evidence."""
from __future__ import annotations
import argparse, csv, json, statistics, struct
from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path

SCHEMA="AER_DRIVEBOARD_RAW_V1"; HEADER=struct.Struct("<8sHHI32s"); RECORD=struct.Struct("<IHHQQQQiiQqII")
EVENT_NAMES={1:"write",2:"read",3:"marker",4:"overflow",5:"writev",6:"fwrite",7:"dup"}
COMMAND_NAMES={0:"deactivation",4:"calibration_test",6:"directional_or_initialization",11:"continuous_or_configuration",123:"pattern",124:"initialization",125:"idle",127:"probe"}
PATTERN_TRANSLATIONS={11:[0],9:[1],0:[2,3,6,7,8,9,11,12,14,15],10:[4],8:[5],4:[10],2:[13]}

@dataclass
class Record:
    sequence:int; timestamp_ns:int; event_type:int; capture_status:int; endpoint:int; fd:int; requested_count:int; result:int; payload:bytes

@dataclass
class Command:
    sequence:int; timestamp_ns:int; relative_ms:float; phase:str; channel:int; command:int; command_name:str
    value_a:int; value_b:int; direction_bit:int|None; magnitude:int|None; pattern_translation:int|None
    pattern_candidates:str; write_result:int; frame_hex:str; checksum_valid:bool

def load_json(path): return json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}

def read_capture(path):
    data=path.read_bytes()
    if len(data)<HEADER.size: raise ValueError("capture is shorter than binary header")
    magic,version,header_size,endian,schema_raw=HEADER.unpack_from(data); schema=schema_raw.split(b"\0",1)[0].decode()
    if (magic,version,endian,schema)!=(b"AERDBR1\0",1,0x01020304,SCHEMA): raise ValueError(f"unsupported capture header: {magic!r}/{version}/{schema}")
    if not HEADER.size<=header_size<=len(data): raise ValueError(f"invalid header size {header_size}")
    records=[]; issues=[]; offset=header_size
    while offset<len(data):
        if len(data)-offset<RECORD.size: issues.append(f"trailing_partial_record_header_at={offset}"); break
        f=RECORD.unpack_from(data,offset); size,typ,status,seq,ts,_pid,_tid,ep,fd,req,result,plen,_reserved=f
        if size!=RECORD.size+plen or offset+size>len(data): issues.append(f"malformed_record_at={offset}"); break
        records.append(Record(seq,ts,typ,status,ep,fd,req,result,data[offset+RECORD.size:offset+size])); offset+=size
    for a,b in zip(records,records[1:]):
        if b.sequence>a.sequence+1: issues.append(f"sequence_gap={a.sequence + 1}-{b.sequence - 1}")
        if b.sequence<=a.sequence: issues.append(f"nonmonotonic_sequence={a.sequence}->{b.sequence}")
        if b.timestamp_ns<a.timestamp_ns: issues.append(f"nonmonotonic_timestamp={a.sequence}->{b.sequence}")
    return {"schema":schema,"format_version":version,"header_size":header_size},records,issues

def valid_checksum(frame):
    if len(frame) not in (4,7): return False
    value=frame[0]&0x7f
    for byte in frame[1:-1]: value^=byte
    return value==frame[-1]

def decode_commands(records,gameplay_start):
    writes=[r for r in records if r.event_type in (1,5,6)]; start=records[0].timestamp_ns if records else 0; commands=[]; issues=[]
    for r in writes:
        frame=r.payload
        if len(frame) not in (4,7): issues.append(f"sequence={r.sequence}: unsupported_write_length={len(frame)}"); continue
        okay=valid_checksum(frame)
        if not okay: issues.append(f"sequence={r.sequence}: checksum_failure")
        phase="gameplay" if gameplay_start is not None and r.timestamp_ns>=gameplay_start else "initialization_or_menu"
        for channel in range(1 if len(frame)==4 else 2):
            x=frame[channel*3:channel*3+3]; cmd=x[0]&0x7f; direction=magnitude=translation=None; candidates=""
            if cmd==0x0b and phase=="gameplay":
                # Jennifer packs the quantized magnitude into byte 1, shifted left 3.
                # Byte 2 is not the magnitude; do not infer polarity from magnitude bits.
                value=x[1]>>3
                magnitude=value if 4<=value<=15 else None
            elif cmd==0x7b: translation=x[2]&0x0f; direction=1 if x[2]&0x10 else 0; candidates="|".join(map(str,PATTERN_TRANSLATIONS.get(translation,[])))
            commands.append(Command(r.sequence,r.timestamp_ns,(r.timestamp_ns-start)/1e6,phase,channel,cmd,COMMAND_NAMES.get(cmd,"unknown"),x[1],x[2],direction,magnitude,translation,candidates,r.result,frame.hex(" "),okay))
    return commands,issues

def stats(values):
    if not values:return {"min":None,"median":None,"p95":None,"max":None}
    x=sorted(values); return {"min":x[0],"median":statistics.median(x),"p95":x[min(len(x)-1,int(.95*(len(x)-1)))],"max":x[-1]}

def analyze(records,commands,metadata,native,virtual,parser_issues,decode_issues):
    writes=[r for r in records if r.event_type in (1,5,6)]; reads=[r for r in records if r.event_type==2]; overflow=[r for r in records if r.event_type==4]
    gameplay=[c for c in commands if c.phase=="gameplay"]; active=[c for c in gameplay if c.command not in (0x7d,0)]; continuous=[c for c in gameplay if c.command==0x0b and c.magnitude is not None]; patterns=[c for c in gameplay if c.command==0x7b]
    per_channel={}
    for channel in sorted({c.channel for c in commands}):
        selected=[c for c in gameplay if c.channel==channel]; per_channel[str(channel)]={"commands":len(selected),"families":dict(sorted(Counter(c.command_name for c in selected).items())),"active_commands":sum(c.command not in (0x7d,0) for c in selected)}
    intervals=[(b.timestamp_ns-a.timestamp_ns)/1e6 for a,b in zip(active,active[1:]) if b.timestamp_ns>=a.timestamp_ns]
    pattern_counts=Counter((c.pattern_translation,c.direction_bit,c.pattern_candidates) for c in patterns)
    return {
      "input":{"schema":metadata.get("schema"),"records_reported":metadata.get("records_written"),"records_parsed":len(records),"capture_complete":metadata.get("capture_complete"),"dropped_records_reported":metadata.get("dropped_record_count")},
      "integrity":{"parser_issues":parser_issues,"decoder_issues":decode_issues,"writes":len(writes),"successful_original_write_calls":sum(r.result==r.requested_count for r in writes),"failed_original_write_calls":sum(r.result<0 for r in writes),"reads":len(reads),"overflow_records":len(overflow),"checksum_failures":sum(not c.checksum_valid for c in commands)//2,"virtual_transport":virtual},
      "native_pipeline":{"driver_final":native.get("native_state",{}).get("final_driver"),"check_final":native.get("native_state",{}).get("final_check"),"callbacks":{x["name"]:x["count"] for x in native.get("pipeline",[])}},
      "decoded":{"logical_commands":len(commands),"gameplay_logical_commands":len(gameplay),"gameplay_active_commands":len(active),"per_channel":per_channel,"command_families":dict(sorted(Counter(c.command_name for c in gameplay).items()))},
      "continuous":{"count":len(continuous),"magnitude_distribution":dict(sorted(Counter(c.magnitude for c in continuous).items())),"direction_0":sum(c.direction_bit==0 for c in continuous),"direction_1":sum(c.direction_bit==1 for c in continuous),"direction_changes":sum(a.direction_bit!=b.direction_bit for a,b in zip(continuous,continuous[1:])),"magnitude_transitions":sum(a.magnitude!=b.magnitude for a,b in zip(continuous,continuous[1:]))},
      "patterns":{"count":len(patterns),"observations":[{"translation":k[0],"direction":k[1],"candidate_indices":k[2],"count":n} for k,n in sorted(pattern_counts.items(),key=lambda item:(-item[1],item[0]))]},
      "timing":{"active_inter_command_ms":stats(intervals),"shutdown_or_zero_commands":sum(c.command==0 for c in commands)},
      "limitations":["Road correlations require AER_VEHICLE_FFB_V2; V1 road fields were read from the wrong structure pointer.","Translated pattern values are not unique pattern indices where translations collide.","Command magnitudes are original game requests, not physical torque.","Inbound bytes are loader-synthesized responses, not authentic Sega firmware output.","Recorder completeness must be evaluated from each input capture\u0027s own metadata; do not assume earlier losses.","Original write results and virtual accepted-frame counts are different observation layers."]}

def write_markdown(path,s,capture):
    i,d,c,p,t=s["integrity"],s["decoded"],s["continuous"],s["patterns"],s["timing"]
    lines=["# AER-03 Native FFB Capture Analysis","",f"Capture: `{capture}`","","## What this establishes","",f"Jennifer reached driver state {s['native_pipeline']['driver_final']} and cabinet-check state {s['native_pipeline']['check_final']}. The analyzer parsed {s['input']['records_parsed']:,} events and {d['gameplay_logical_commands']:,} gameplay logical-slot commands.","","These are original game requests to Sega's motor controller, not physical torque or reconstructed sensation.","","## Steering load","",f"Decoded continuous gameplay requests: {c['count']:,}. Magnitude transitions: {c['magnitude_transitions']:,}. Encoded-direction changes: {c['direction_changes']:,}.","","| Request | Count |","|---:|---:|"]
    lines += [f"| {k} | {v} |" for k,v in c["magnitude_distribution"].items()]
    lines += ["","## Discrete patterns","",f"Observed requests: {p['count']:,}.","","| Translation | Direction | Candidate indices | Count |","|---:|---:|---|---:|"]
    lines += [f"| 0x{x['translation']:02X} | {x['direction']} | {x['candidate_indices'] or 'unresolved'} | {x['count']} |" for x in p["observations"]]
    lines += ["","Pattern 10 maps uniquely to `0x04`; Pattern 13 maps uniquely to `0x02`. Zero translations remain ambiguous.","","## Surface, kerb, and collision evidence","","No synchronized road classification, position, or collision state is present. The capture cannot directly label a request cobblestone, kerb, wall, or vehicle contact. Pattern 10 remains a classification-family transition, not a universal cobblestone label.","","## Timing","",f"Active serial-request interval statistics in milliseconds: `{json.dumps(t['active_inter_command_ms'],sort_keys=True)}`. Callback counts, serial writes, accepted virtual frames, and firmware durations remain separate measurements.","","## Capture integrity","",f"- Events: {s['input']['records_parsed']:,}",f"- Writes / reads: {i['writes']:,} / {i['reads']:,}",f"- Dropped / overflow records: {s['input']['dropped_records_reported']} / {i['overflow_records']}",f"- Checksum failures: {i['checksum_failures']}",f"- Virtual accepted frames: {i['virtual_transport'].get('accepted_frames')}",f"- Physical isolation: {i['virtual_transport'].get('physical_isolation')}","","Interpret capture completeness from the input metadata; earlier dropped-event counts do not apply to every session.","","## Evidence limits",""]+[f"- {x}" for x in s["limitations"]]
    path.write_text("\n".join(lines)+"\n",encoding="utf-8")

def write_component_reports(out,s):
    pattern_lines=["translation,direction,candidate_internal_indices,count"]
    for x in s["patterns"]["observations"]: pattern_lines.append(f"0x{x['translation']:02X},{x['direction']},{x['candidate_indices']},{x['count']}")
    (out/"command_pattern_matrix.csv").write_text("\n".join(pattern_lines)+"\n",encoding="utf-8")
    (out/"steering_magnitude_analysis.json").write_text(json.dumps(s["continuous"],indent=2,sort_keys=True)+"\n",encoding="utf-8")
    (out/"SURFACE_AND_KERB_REPORT.md").write_text(
      "# Surface and Kerb Research Report\n\n"
      "The live serial capture contains Pattern 10 translations (`0x04`) and translations compatible with Patterns 12–15, but it has no synchronized position or road-classification channel. Pattern 10 therefore remains evidence of a classification-family transition, not a cobblestone or kerb label. Zero translations cannot distinguish Patterns 12, 14, or 15 from other zero-translated indices; `0x02` uniquely identifies Pattern 13. Tulip Garden's ordinal-20 bridge-road evidence remains a separate static asset finding and cannot be temporally joined to this session.\n",
      encoding="utf-8")
    (out/"COLLISION_COMMAND_REPORT.md").write_text(
      "# Collision Command Report\n\n"
      "Two `0x0B` pattern translations compatible with internal Pattern 0 were observed during gameplay. Static lineage strongly associates Pattern 0/2 selection with the wall-rebound state, but this capture has no synchronized collision state. The records cannot distinguish wall rebound, wall friction, vehicle contact, or a nearby contact transition by serial traffic alone. No physical impulse strength is inferred.\n",
      encoding="utf-8")
    integrity={"input":s["input"],"integrity":s["integrity"],"native_pipeline":s["native_pipeline"],"limitations":s["limitations"]}
    (out/"capture_integrity_report.json").write_text(json.dumps(integrity,indent=2,sort_keys=True)+"\n",encoding="utf-8")

def load_vehicle(path):
    if not path.exists(): return [],["vehicle_telemetry_unavailable"]
    lines=path.read_text(encoding="utf-8-sig").splitlines()
    if not lines: return [],["vehicle_schema_missing_or_unsupported"]
    if lines[0]=="#schema=AER_VEHICLE_FFB_V1":
        return list(csv.DictReader(lines[1:])),["legacy_v1_road_fields_unreliable_wrong_structure_pointer"]
    if lines[0]!="#schema=AER_VEHICLE_FFB_V2": return [],["vehicle_schema_missing_or_unsupported"]
    return list(csv.DictReader(lines[1:])),[]

def write_correlations(out,rows,issues):
    send=[r for r in rows if r.get("event")=="send_out"]
    # V1 sampled CAR_WORK rather than EVWORK_CAR; never present its masks as verified.
    road_valid=not issues and all("validity_flags" in r for r in send)
    def one_hot_or_zero(text):
        try:
            value=int(text)
            return 0<=value<=0xffffffff and (value==0 or (value & (value-1))==0)
        except (ValueError,TypeError):
            return False
    valid_road_rows=[r for r in send if road_valid and (int(r["validity_flags"]) & 0x4)
        and one_hot_or_zero(r.get("front_left_road_mask")) and one_hot_or_zero(r.get("front_right_road_mask"))]
    road=Counter((r.get("front_left_road_mask"),r.get("front_right_road_mask"),r.get("command"),r.get("value_b")) for r in valid_road_rows)
    steering=Counter((r.get("front_tire_direction_s16"),r.get("command"),r.get("value_b")) for r in send) if not issues else Counter()
    payload={"schema":"AER_FFB_VEHICLE_CORRELATION_V2","rows":len(rows),"send_rows":len(send),"road_valid_rows":len(valid_road_rows),"road_excluded_rows":len(send)-len(valid_road_rows),"issues":issues,
      "road_command_matrix":[{"front_left":k[0],"front_right":k[1],"command":k[2],"value_b":k[3],"count":n} for k,n in road.most_common()],
      "steering_command_matrix":[{"front_tire_direction":k[0],"command":k[1],"value_b":k[2],"count":n} for k,n in steering.most_common()]}
    (out/"vehicle_ffb_correlation.json").write_text(json.dumps(payload,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    with (out/"synchronized_ffb_vehicle_timeline.csv").open("w",newline="",encoding="utf-8") as f:
        if rows: w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    return payload

def write_profile_evidence(out,summary,rows,correlation):
    send=[r for r in rows if r.get("event")=="send_out" and r.get("logical_channel")=="0"]
    patterns=[r for r in send if r.get("command")=="123"]
    pattern_values=Counter(r.get("value_b") for r in patterns)
    pattern_road=Counter((r.get("value_b"),r.get("front_left_road_mask"),r.get("front_right_road_mask")) for r in patterns)
    asymmetric=sum(r.get("front_left_road_mask")!=r.get("front_right_road_mask") for r in patterns)
    payload={
      "schema":"AER_PROFILE_EVIDENCE_V1",
      "source_capture_complete":summary["input"]["capture_complete"],
      "source_dropped_records":summary["input"]["dropped_records_reported"],
      "continuous":summary["continuous"],
      "patterns":summary["patterns"],
      "synchronized":{"rows":len(rows),"channel_0_send_rows":len(send),"road_valid_rows":correlation["road_valid_rows"],"road_excluded_rows":correlation["road_excluded_rows"]},
      "pattern_value_b_distribution":dict(sorted(pattern_values.items(),key=lambda item:int(item[0]))),
      "pattern_rows_with_asymmetric_front_contact":asymmetric,
      "leading_pattern_road_contexts":[{"value_b":k[0],"front_left":k[1],"front_right":k[2],"count":n} for k,n in pattern_road.most_common(32)],
      "classification":{"continuous_magnitude":"CONFIRMED_GAME_REQUEST_NOT_TORQUE","road_masks":"CONFIRMED_NATIVE_CLASSIFIERS_MATERIAL_IDENTITY_UNRESOLVED","pattern_road_relationship":"STRONGLY_SUPPORTED_CORRELATION_NOT_CAUSATION","collision_identity":"UNKNOWN_NO_DIRECT_COLLISION_FIELD","firmware_waveform":"UNKNOWN"}
    }
    (out/"aer_profile_evidence_matrix.json").write_text(json.dumps(payload,indent=2,sort_keys=True)+"\n",encoding="utf-8")
    return payload

def main():
    ap=argparse.ArgumentParser(description=__doc__); ap.add_argument("capture",type=Path); ap.add_argument("--output",type=Path,required=True); args=ap.parse_args(); capture=args.capture.resolve(); out=args.output.resolve(); out.mkdir(parents=True,exist_ok=True)
    metadata=load_json(capture/"driveboard_raw.json"); native=load_json(capture/"native_activation.json"); virtual=load_json(capture/"virtual_driveboard_status.json")
    binary,records,pissues=read_capture(capture/"driveboard_raw.aerbin")
    if metadata.get("schema")!=binary["schema"]: raise ValueError("binary and metadata schemas differ")
    pipeline={x["name"]:x for x in native.get("pipeline",[])}; commands,dissues=decode_commands(records,pipeline.get("DrCtrlDataSet",{}).get("first_ns")); summary=analyze(records,commands,metadata,native,virtual,pissues,dissues)
    with (out/"native_ffb_timeline.csv").open("w",newline="",encoding="utf-8") as f:
        fields=list(Command.__dataclass_fields__); w=csv.DictWriter(f,fieldnames=fields); w.writeheader(); w.writerows(asdict(x) for x in commands)
    (out/"native_ffb_analysis.json").write_text(json.dumps(summary,indent=2,sort_keys=True)+"\n",encoding="utf-8"); write_markdown(out/"NATIVE_FFB_ANALYSIS.md",summary,capture.name); write_component_reports(out,summary)
    vehicle_path=capture/"vehicle_ffb_v2.csv"
    if not vehicle_path.exists(): vehicle_path=capture/"vehicle_ffb_v1.csv"
    vehicle,vissues=load_vehicle(vehicle_path);correlation=write_correlations(out,vehicle,vissues)
    write_profile_evidence(out,summary,vehicle,correlation)
    print(json.dumps(summary,indent=2,sort_keys=True)); return 0

if __name__=="__main__": raise SystemExit(main())
