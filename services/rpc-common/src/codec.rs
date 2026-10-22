//! Tokio codec for LSP-style framing:
//! Content-Length: <n>\r\n\r\n<body>

use bytes::{Buf, BufMut, BytesMut};
use tokio_util::codec::{Decoder, Encoder};

pub struct LspFrameCodec;

impl Decoder for LspFrameCodec {
    type Item = BytesMut;
    type Error = std::io::Error;

    fn decode(&mut self, src: &mut BytesMut) -> Result<Option<Self::Item>, Self::Error> {
        // Look for \r\n\r\n
        let header_end = match find_subsequence(src, b"\r\n\r\n") {
            Some(i) => i,
            None => return Ok(None),
        };

        let header = &src[..header_end];
        let content_length = parse_content_length(header).ok_or_else(|| {
            std::io::Error::new(std::io::ErrorKind::InvalidData, "missing Content-Length")
        })?;

        let body_start = header_end + 4;
        if src.len() < body_start + content_length {
            return Ok(None); // wait for more data
        }

        src.advance(body_start);
        let body = src.split_to(content_length);
        Ok(Some(body))
    }
}

impl Encoder<Vec<u8>> for LspFrameCodec {
    type Error = std::io::Error;

    fn encode(&mut self, item: Vec<u8>, dst: &mut BytesMut) -> Result<(), Self::Error> {
        let header = format!("Content-Length: {}\r\n\r\n", item.len());
        dst.put_slice(header.as_bytes());
        dst.put_slice(&item);
        Ok(())
    }
}

fn find_subsequence(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    haystack.windows(needle.len()).position(|w| w == needle)
}

fn parse_content_length(header: &[u8]) -> Option<usize> {
    let s = std::str::from_utf8(header).ok()?;
    for line in s.lines() {
        if let Some((name, value)) = line.split_once(':')
            && name.trim().eq_ignore_ascii_case("content-length")
        {
            return value.trim().parse().ok();
        }
    }
    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use bytes::BytesMut;

    #[test]
    fn round_trip_frame() {
        let payload = b"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"$/ping\"}";
        let mut buf = BytesMut::new();
        LspFrameCodec.encode(payload.to_vec(), &mut buf).unwrap();

        let decoded = LspFrameCodec.decode(&mut buf).unwrap().unwrap();
        assert_eq!(decoded.as_ref(), payload.as_ref());
        assert!(buf.is_empty());
    }

    #[test]
    fn partial_body_returns_none() {
        let payload = b"{\"jsonrpc\":\"2.0\"}";
        let header = format!("Content-Length: {}\r\n\r\n", payload.len() + 10);
        let mut buf = BytesMut::from(header.as_bytes());
        buf.extend_from_slice(payload); // body is shorter than claimed
        assert!(LspFrameCodec.decode(&mut buf).unwrap().is_none());
    }

    #[test]
    fn content_length_header_name_is_case_insensitive() {
        let payload = b"{}";
        let mut buf = BytesMut::from(b"content-length: 2\r\n\r\n".as_slice());
        buf.extend_from_slice(payload);

        assert_eq!(
            LspFrameCodec.decode(&mut buf).unwrap().unwrap().as_ref(),
            payload.as_slice()
        );
        assert!(buf.is_empty());
    }

    #[test]
    fn multiple_frames_are_decoded_sequentially() {
        let mut buf = BytesMut::new();
        LspFrameCodec.encode(b"first".to_vec(), &mut buf).unwrap();
        LspFrameCodec.encode(b"second".to_vec(), &mut buf).unwrap();

        assert_eq!(
            LspFrameCodec.decode(&mut buf).unwrap().unwrap().as_ref(),
            b"first"
        );
        assert_eq!(
            LspFrameCodec.decode(&mut buf).unwrap().unwrap().as_ref(),
            b"second"
        );
        assert!(buf.is_empty());
    }

    #[test]
    fn invalid_content_length_is_rejected() {
        let mut buf = BytesMut::from(b"Content-Length: invalid\r\n\r\n".as_slice());

        let error = LspFrameCodec.decode(&mut buf).unwrap_err();
        assert_eq!(error.kind(), std::io::ErrorKind::InvalidData);
    }
}
