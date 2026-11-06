// File: installer/src/update.rs
// Purpose: Update check logic: version comparison, release JSON reading and URL allowlists (no network code here)
// Author: Akilesh
// Date: 2026-11-05

/// The version of this build: the release tag it was stamped with, else the crate version.
pub const VERSION: &str = env!("CROSSPORT_VERSION");

/// The only address the check queries.
pub const RELEASES_API_URL: &str =
    "https://api.github.com/repos/ItsAkilesh/crossport/releases/latest";
pub const API_HOST: &str = "api.github.com";
const DOWNLOAD_HOST: &str = "github.com";
const RELEASE_PATH_PREFIX: &str = "/ItsAkilesh/crossport/releases/";
/// Name of the file a release publishes for people to download.
pub const SETUP_ASSET: &str = "Crossport-Setup.exe";

/// The newest published release, as far as this program needs to know.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Release {
    pub version: String,
    /// The release page, always on the allowlisted host and path.
    pub page: String,
    /// The direct download of the setup file, when the release has one on the allowlisted host and path.
    pub setup_url: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Outcome {
    UpToDate,
    Available(Release),
}

/// `major.minor.patch` from a tag such as `v0.8.1`. Pre-release and build suffixes are not versions we offer.
pub fn parse_version(text: &str) -> Option<[u64; 3]> {
    let mut parts = text.trim().trim_start_matches('v').split('.');
    let mut next = || {
        parts
            .next()
            .filter(|p| !p.is_empty() && p.bytes().all(|b| b.is_ascii_digit()))
            .and_then(|p| p.parse().ok())
    };
    let version = [next()?, next()?, next()?];
    parts.next().is_none().then_some(version)
}

pub fn is_newer(current: &str, candidate: &str) -> bool {
    match (parse_version(current), parse_version(candidate)) {
        (Some(have), Some(offered)) => offered > have,
        _ => false,
    }
}

/// The host and path of an `https://` address that has no user name, port, query or fragment tricks.
pub fn split_https(url: &str) -> Option<(&str, &str)> {
    let rest = url.strip_prefix("https://")?;
    let (host, path) = rest
        .find('/')
        .map_or((rest, "/"), |i| (&rest[..i], &rest[i..]));
    let clean_host = !host.is_empty()
        && host
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b == b'.' || b == b'-');
    let clean_path = !path.contains(['\\', '@', '#', '?', ' ']) && !path.contains("..");
    (clean_host && clean_path).then_some((host, path))
}

/// True for an address on `github.com` under this project's releases.
pub fn is_release_url(url: &str) -> bool {
    matches!(split_https(url), Some((host, path)) if host == DOWNLOAD_HOST && path.starts_with(RELEASE_PATH_PREFIX))
}

/// Every string value of `"key": "value"` in a JSON text, with the common escapes undone.
pub fn json_strings(body: &str, key: &str) -> Vec<String> {
    let needle = format!("\"{key}\"");
    let mut found = Vec::new();
    for (at, _) in body.match_indices(&needle) {
        let after = body[at + needle.len()..].trim_start();
        let Some(value) = after
            .strip_prefix(':')
            .map(str::trim_start)
            .and_then(|v| v.strip_prefix('"'))
        else {
            continue;
        };
        found.push(unescape(value));
    }
    found
}

fn unescape(raw: &str) -> String {
    let mut out = String::new();
    let mut chars = raw.chars();
    while let Some(c) = chars.next() {
        match c {
            '"' => break,
            '\\' => match chars.next() {
                Some('n') => out.push('\n'),
                Some('t') => out.push('\t'),
                Some(other) => out.push(other),
                None => break,
            },
            c => out.push(c),
        }
    }
    out
}

/// Read the latest-release answer of the GitHub API. `None` when it names no usable version or page.
pub fn parse_release(body: &str) -> Option<Release> {
    let tag = json_strings(body, "tag_name").into_iter().next()?;
    parse_version(&tag)?;
    let page = json_strings(body, "html_url")
        .into_iter()
        .find(|u| is_release_url(u))?;
    let setup_url = json_strings(body, "browser_download_url")
        .into_iter()
        .find(|u| is_release_url(u) && u.ends_with(&format!("/{SETUP_ASSET}")));
    Some(Release {
        version: tag.trim_start_matches('v').to_string(),
        page,
        setup_url,
    })
}

/// Compare `current` with the release `fetch` returns for the allowlisted address.
pub fn check(
    current: &str,
    fetch: impl FnOnce(&str) -> Result<String, String>,
) -> Result<Outcome, String> {
    let body = fetch(RELEASES_API_URL)?;
    let release =
        parse_release(&body).ok_or("the release information was not in the expected form")?;
    Ok(if is_newer(current, &release.version) {
        Outcome::Available(release)
    } else {
        Outcome::UpToDate
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE: &str = r#"{"url":"https:\/\/api.github.com\/x","html_url":"https://github.com/ItsAkilesh/crossport/releases/tag/v0.8.1",
      "tag_name": "v0.8.1","assets":[{"name":"SHA256SUMS.txt","browser_download_url":"https://github.com/ItsAkilesh/crossport/releases/download/v0.8.1/SHA256SUMS.txt"},
      {"name":"Crossport-Setup.exe","browser_download_url":"https://github.com/ItsAkilesh/crossport/releases/download/v0.8.1/Crossport-Setup.exe"}]}"#;

    #[test]
    fn versions_compare_numerically() {
        assert!(is_newer("0.8.0", "v0.8.1"));
        assert!(is_newer("0.9.9", "0.10.0"));
        assert!(!is_newer("0.8.1", "0.8.1"));
        assert!(!is_newer("1.0.0", "0.9.9"));
    }

    #[test]
    fn odd_versions_are_never_offered() {
        for bad in [
            "",
            "v1",
            "1.2",
            "1.2.3.4",
            "1.2.3-rc1",
            "1.x.3",
            "..",
            "-1.0.0",
        ] {
            assert_eq!(parse_version(bad), None, "{bad}");
            assert!(!is_newer("0.0.1", bad));
        }
    }

    #[test]
    fn reads_the_tag_page_and_download() {
        let release = parse_release(SAMPLE).unwrap();
        assert_eq!(release.version, "0.8.1");
        assert!(release.page.ends_with("/tag/v0.8.1"));
        assert!(release.setup_url.unwrap().ends_with("/Crossport-Setup.exe"));
    }

    #[test]
    fn a_page_off_the_allowlist_is_refused() {
        let hostile = SAMPLE.replace("github.com/ItsAkilesh", "evil.example/ItsAkilesh");
        assert_eq!(parse_release(&hostile), None);
        let other_repo = SAMPLE.replace(
            "ItsAkilesh/crossport/releases/tag",
            "someone/else/releases/tag",
        );
        assert_eq!(parse_release(&other_repo), None);
    }

    #[test]
    fn a_download_off_the_allowlist_is_dropped_but_the_page_stays() {
        let moved = SAMPLE.replace(
            "releases/download/v0.8.1/Crossport-Setup.exe",
            "raw/x/Crossport-Setup.exe",
        );
        let release = parse_release(&moved).unwrap();
        assert_eq!(release.setup_url, None);
    }

    #[test]
    fn url_allowlist_rejects_tricks() {
        let good = "https://github.com/ItsAkilesh/crossport/releases/tag/v1.0.0";
        assert!(is_release_url(good));
        for bad in [
            "http://github.com/ItsAkilesh/crossport/releases/x",
            "https://github.com.evil.example/ItsAkilesh/crossport/releases/x",
            "https://user@github.com/ItsAkilesh/crossport/releases/x",
            "https://github.com:444/ItsAkilesh/crossport/releases/x",
            "https://github.com/ItsAkilesh/crossport/releases/../../x",
            "https://github.com/ItsAkilesh/crossport/releases/x?y=1",
            "https://github.com/ItsAkilesh/crossport-evil/releases/x",
            "https://github.com\\@evil.example/ItsAkilesh/crossport/releases/x",
        ] {
            assert!(!is_release_url(bad), "{bad}");
        }
    }

    #[test]
    fn check_only_asks_the_allowlisted_address() {
        let mut asked = String::new();
        let outcome = check("0.1.0", |url| {
            asked = url.to_string();
            Ok(SAMPLE.to_string())
        });
        assert_eq!(
            split_https(&asked),
            Some((API_HOST, "/repos/ItsAkilesh/crossport/releases/latest"))
        );
        assert!(matches!(outcome, Ok(Outcome::Available(_))));
        assert_eq!(
            check("0.8.1", |_| Ok(SAMPLE.to_string())),
            Ok(Outcome::UpToDate)
        );
    }

    #[test]
    fn network_and_format_failures_are_reported() {
        assert_eq!(
            check("0.1.0", |_| Err("offline".into())),
            Err("offline".into())
        );
        assert!(check("0.1.0", |_| Ok("{}".into())).is_err());
        assert!(check("0.1.0", |_| Ok("not json at all".into())).is_err());
    }
}
