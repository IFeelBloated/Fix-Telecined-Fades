project('Fix Telecined Fades',
        'cpp',
        default_options : ['cpp_std=c++14'],
        license : 'GPL-3.0',
        version : '8')

# Deps
soup = dependency('vapoursynth', version : '>= 0')

# Sources
sources = [
    'Source.cpp']

sources_avxfma = [
    'Source_AVX_FMA.cpp']

sources_asm = [
    'cpu.asm']

# Assembler
yasm_opts = ['-Dprivate_prefix=ftf']
if host_machine.cpu_family() == 'x86'
    yasm_opts += ['-DARCH_X86_64=0']
    bits = '32'
endif
if host_machine.cpu_family() == 'x86_64'
    yasm_opts += ['-DARCH_X86_64=1', '-DPIC']
    bits = '64'
endif
if host_machine.system() == 'linux'
    yasm_opts += ['-f', 'elf@0@'.format(bits)]
endif
if host_machine.system() == 'darwin'
    yasm_opts += ['-f', 'macho@0@'.format(bits), '-DPREFIX']
endif
if host_machine.system() == 'windows'
    yasm_opts += ['-f', 'win@0@'.format(bits)]
endif
yasm_opts += ['@INPUT@', '-o', '@OUTPUT@']

yasm = find_program('yasm')
asm_gen = generator(yasm,
                    output : '@BASENAME@.obj',
                    arguments : yasm_opts)
objs_asm = asm_gen.process(sources_asm)

# Libs
avxfma = static_library(
    'avxfma',
    [sources_avxfma, objs_asm],
    cpp_args : ['-mavx', '-mfma'],
    dependencies : soup,
    pic : true,
    install : false)

library(
    'fixtelecinedfades',
    [sources, objs_asm],
    link_with : avxfma,
    dependencies : soup,
    install_dir : join_paths(get_option('libdir'), 'vapoursynth'),
    install : true)
