#!/usr/bin/env python3
"""Read-only validation of a distributable component ZIP (stdlib only)."""
import hashlib, json, sys, zipfile
from pathlib import PurePosixPath
with zipfile.ZipFile(sys.argv[1]) as archive:
    files = {p.filename: archive.read(p) for p in archive.infolist() if not p.is_dir()}
    prefix = 'yoke_voice/'
    assert files and all(n.startswith(prefix) for n in files)
    for name in files:
        path = PurePosixPath(name)
        assert not path.is_absolute() and '..' not in path.parts
        assert not any(p in {'internal','managed_components','.git','__pycache__'} for p in path.parts)
        assert path.name not in {'sdkconfig','sdkconfig.old','dependencies.lock'}
        assert not path.name.endswith(('.obj','.o','.elf','.pyc'))
        if 'ci1302_protocol' in path.parts:
            assert path.suffix not in {'.c','.cpp','.cc','.S'}
    manifest = json.loads(files[prefix+'PACKAGE_SHA256.json'])
    assert set(manifest) == {n[len(prefix):] for n in files if n != prefix+'PACKAGE_SHA256.json'}
    for name, expected in manifest.items():
        assert hashlib.sha256(files[prefix+name]).hexdigest() == expected, name
    info = json.loads(files[prefix+'ci1302_protocol/build_info.json'])
    assert info['schema_version'] == 2 and info['target'] == 'esp32s3'
    assert set(info['variants']) == {'idf5', 'idf6'}
    expected_archives = set()
    for series, version in [('idf5', '5.5.1'), ('idf6', '6.2.0')]:
        relative = f'lib/esp32s3/{series}/libci1302_protocol.a'
        binary = prefix+'ci1302_protocol/'+relative
        expected_archives.add(binary)
        assert files[binary].startswith(b'!<arch>\n')
        variant = info['variants'][series]
        metadata_path = f'lib/esp32s3/{series}/build_info.json'
        metadata = json.loads(files[prefix+'ci1302_protocol/'+metadata_path])
        assert variant['archive'] == relative and variant['metadata'] == metadata_path
        assert variant['idf_version'] == metadata['idf_version'] == version
        assert metadata['idf_series'] == series and metadata['libc'] == 'newlib'
        assert metadata['debug_symbols'] is False and metadata['uart_isr_in_iram'] is True
        assert hashlib.sha256(files[binary]).hexdigest() == variant['archive_sha256'] == metadata['archive_sha256']
    assert {name for name in files if name.endswith('.a')} == expected_archives
    for name in ['player_demo','gmf_play_file','gmf_play_http','gmf_record_wav','gmf_record_muxer']:
        base = prefix+'examples/'+name+'/'
        for required in ['CMakeLists.txt','sdkconfig.defaults','main/CMakeLists.txt','main/main.c']:
            assert base+required in files, base+required
        assert b'CONFIG_ROOTMAKER_WIFI_PASSWORD=' not in files[base+'sdkconfig.defaults']
    for name in ['player_demo', 'gmf_play_http']:
        base = prefix+'examples/'+name+'/main/'
        assert b'#define EXAMPLE_WIFI_SSID ""' in files[base+'main.c']
        assert b'#define EXAMPLE_WIFI_PASSWORD ""' in files[base+'main.c']
        assert b'example_network_connect' in files[base+'main.c']
    for name in ['gmf_play_file','gmf_play_http','gmf_record_wav','gmf_record_muxer']:
        source = files[prefix+'examples/'+name+'/main/pipeline.c']
        assert b'esp_gmf_pool_new_pipeline' in source
        assert b'yoke_voice_player' not in source
print('Package inventory, checksums, binary-only protocol and five complete examples: PASS')
