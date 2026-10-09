#!/usr/bin/env python3
import importlib.util, json, sys, tempfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]; MODULE=ROOT/"research/aer/tools/ffb_analyzer/analyze.py"
spec=importlib.util.spec_from_file_location("aer_ffb_analyzer",MODULE); analyzer=importlib.util.module_from_spec(spec); sys.modules[spec.name]=analyzer; spec.loader.exec_module(analyzer)

def frame(*triples):
    body=bytearray(sum((list(x) for x in triples),[])); body[0]|=0x80; checksum=body[0]&0x7f
    for value in body[1:]: checksum^=value
    return bytes(body+bytes([checksum]))

def main():
    single=frame((0x0b,0x20,0x10)); dual=frame((0x7b,0,0x14),(0x7d,0,0))
    assert len(single)==4 and analyzer.valid_checksum(single); assert len(dual)==7 and analyzer.valid_checksum(dual)
    assert not analyzer.valid_checksum(dual[:-1]+b"\0")
    records=[analyzer.Record(1,100,1,0,1,9,7,7,dual),analyzer.Record(2,200,1,0,1,9,7,7,frame((0x0b,0x20,0x10),(0x7d,0,0)))]
    commands,issues=analyzer.decode_commands(records,150); assert not issues and len(commands)==4
    assert commands[0].pattern_translation==4 and commands[0].direction_bit==1 and commands[0].pattern_candidates=="10"
    assert commands[2].magnitude==4 and commands[2].direction_bit is None and commands[3].command_name=="idle"
    malformed=[analyzer.Record(3,300,1,0,1,9,3,-1,b"\x01\x02\x03")]; _,decode_problems=analyzer.decode_commands(malformed,0); assert "unsupported_write_length=3" in decode_problems[0]
    with tempfile.TemporaryDirectory() as temp:
        path=Path(temp)/"x.aerbin"; path.write_bytes(analyzer.HEADER.pack(b"AERDBR1\0",1,analyzer.HEADER.size,0x01020304,b"AER_DRIVEBOARD_RAW_V1")+b"\1\2")
        _,parsed,problems=analyzer.read_capture(path); assert not parsed and problems==[f"trailing_partial_record_header_at={analyzer.HEADER.size}"]
        vehicle=Path(temp)/"vehicle.csv";vehicle.write_text("#schema=AER_VEHICLE_FFB_V1\nsequence,timestamp_ns,event,validity_flags,front_left_road_mask,front_right_road_mask,command,value_b,front_tire_direction_s16\n1,10,send_out,4,4,4,123,2,-30\n")
        rows,vissues=analyzer.load_vehicle(vehicle);assert "legacy_v1_road_fields_unreliable_wrong_structure_pointer" in vissues
        analyzer.write_correlations(Path(temp),rows,vissues);correlation=json.loads((Path(temp)/"vehicle_ffb_correlation.json").read_text());assert correlation["road_valid_rows"]==0
        vehicle.write_text("#schema=AER_VEHICLE_FFB_V2\nsequence,timestamp_ns,event,validity_flags,front_left_road_mask,front_right_road_mask,command,value_b,front_tire_direction_s16\n1,10,send_out,4,4,8,123,2,-30\n2,20,send_out,4,0xffffffff,8,123,2,-30\n")
        rows,vissues=analyzer.load_vehicle(vehicle);assert not vissues and len(rows)==2
        analyzer.write_correlations(Path(temp),rows,vissues);correlation=json.loads((Path(temp)/"vehicle_ffb_correlation.json").read_text());assert correlation["road_valid_rows"]==1 and correlation["road_excluded_rows"]==1
    print("AER FFB analyzer tests passed")

if __name__=="__main__": main()
