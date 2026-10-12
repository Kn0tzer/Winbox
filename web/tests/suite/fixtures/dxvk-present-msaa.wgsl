struct present_info_t {
    src_offset: vec2<i32>,
    dst_offset: vec2<i32>,
}

const c_samples: u32 = 4u;
const s_gamma_bound: bool = false;

var<private> gl_FragCoord_1: vec4<f32>;
var<immediate> unnamed: present_info_t;
var<private> o_color: vec4<f32>;
@group(0) @binding(0) 
var s_image: texture_multisampled_2d<f32>;
@group(0) @binding(2) 
var s_image_u0024_spl: sampler;
@group(0) @binding(1) 
var s_gamma: texture_1d<f32>;
@group(0) @binding(3) 
var s_gamma_u0024_spl: sampler;

fn main_1() {
    var coord: vec2<i32>;
    var i: u32;

    let _e17 = gl_FragCoord_1;
    let _e21 = unnamed.src_offset;
    let _e24 = unnamed.dst_offset;
    coord = ((vec2<i32>(_e17.xy) + _e21) - _e24);
    let _e26 = coord;
    let _e27 = textureLoad(s_image, _e26, 0i);
    o_color = _e27;
    i = 1u;
    loop {
        let _e28 = i;
        if (_e28 < c_samples) {
            let _e30 = coord;
            let _e31 = i;
            let _e33 = textureLoad(s_image, _e30, bitcast<i32>(_e31));
            let _e34 = o_color;
            o_color = (_e34 + _e33);
            continue;
        } else {
            break;
        }
        continuing {
            let _e36 = i;
            i = (_e36 + bitcast<u32>(1i));
        }
    }
    let _e40 = o_color;
    o_color = (_e40 / vec4(f32(c_samples)));
    if s_gamma_bound {
        let _e44 = o_color[0u];
        let _e45 = textureSample(s_gamma, s_gamma_u0024_spl, _e44);
        let _e48 = o_color[1u];
        let _e49 = textureSample(s_gamma, s_gamma_u0024_spl, _e48);
        let _e52 = o_color[2u];
        let _e53 = textureSample(s_gamma, s_gamma_u0024_spl, _e52);
        let _e56 = o_color[3u];
        o_color = vec4<f32>(_e45.x, _e49.y, _e53.z, _e56);
    }
    return;
}

@fragment 
fn main(@builtin(position) gl_FragCoord: vec4<f32>) -> @location(0) vec4<f32> {
    gl_FragCoord_1 = gl_FragCoord;
    main_1();
    let _e3 = o_color;
    return _e3;
}
