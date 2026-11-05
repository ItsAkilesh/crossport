// File: installer/src/ico.rs
// Purpose: Wrap a PNG image in an .ico container so Windows shortcuts and Apps and features can show it
// Author: Akilesh
// Date: 2026-11-05

const PNG_SIGNATURE: [u8; 8] = [0x89, b'P', b'N', b'G', 0x0D, 0x0A, 0x1A, 0x0A];
const IHDR_WIDTH_AT: usize = 16;
const IHDR_END: usize = 24;
const ICO_HEADER: usize = 6;
const ICO_ENTRY: usize = 16;
const MAX_ICO_SIDE: u32 = 256;

/// An icon file holding the PNG as its only image (Windows Vista and later read PNG inside ICO), or `None`
/// if the data is not a PNG or is larger than 256 pixels on a side.
pub fn png_to_ico(png: &[u8]) -> Option<Vec<u8>> {
    if png.len() < IHDR_END || png[..8] != PNG_SIGNATURE {
        return None;
    }
    let dimension = |at: usize| u32::from_be_bytes([png[at], png[at + 1], png[at + 2], png[at + 3]]);
    let (width, height) = (dimension(IHDR_WIDTH_AT), dimension(IHDR_WIDTH_AT + 4));
    if width == 0 || height == 0 || width > MAX_ICO_SIDE || height > MAX_ICO_SIDE {
        return None;
    }
    let side = |n: u32| if n == MAX_ICO_SIDE { 0u8 } else { n as u8 };
    let mut ico = vec![0, 0, 1, 0, 1, 0];
    ico.extend_from_slice(&[side(width), side(height), 0, 0, 1, 0, 32, 0]);
    ico.extend_from_slice(&(png.len() as u32).to_le_bytes());
    ico.extend_from_slice(&((ICO_HEADER + ICO_ENTRY) as u32).to_le_bytes());
    ico.extend_from_slice(png);
    Some(ico)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fake_png(width: u32, height: u32) -> Vec<u8> {
        let mut png = PNG_SIGNATURE.to_vec();
        png.extend_from_slice(&[0, 0, 0, 13]);
        png.extend_from_slice(b"IHDR");
        png.extend_from_slice(&width.to_be_bytes());
        png.extend_from_slice(&height.to_be_bytes());
        png.extend_from_slice(&[8, 6, 0, 0, 0]);
        png
    }

    #[test]
    fn wraps_a_png() {
        let png = fake_png(48, 48);
        let ico = png_to_ico(&png).unwrap();
        assert_eq!(&ico[..6], &[0, 0, 1, 0, 1, 0]);
        assert_eq!((ico[6], ico[7]), (48, 48));
        assert_eq!(u32::from_le_bytes([ico[14], ico[15], ico[16], ico[17]]) as usize, png.len());
        assert_eq!(u32::from_le_bytes([ico[18], ico[19], ico[20], ico[21]]), 22);
        assert_eq!(&ico[22..], &png[..]);
    }

    #[test]
    fn size_256_is_written_as_zero() {
        let ico = png_to_ico(&fake_png(256, 256)).unwrap();
        assert_eq!((ico[6], ico[7]), (0, 0));
    }

    #[test]
    fn refuses_other_data_and_big_images() {
        assert!(png_to_ico(b"GIF89a").is_none());
        assert!(png_to_ico(&fake_png(512, 512)).is_none());
        assert!(png_to_ico(&fake_png(0, 8)).is_none());
    }
}
