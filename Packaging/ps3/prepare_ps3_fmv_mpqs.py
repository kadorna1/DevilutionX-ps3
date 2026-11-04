#!/usr/bin/env python3
"""Prepare user-owned Diablo MPQ archives for PS3 Cell VDEC FMV playback.

The original MPQ is copied byte-for-byte, then converted FMV sidecars are
appended to that copy. The original .smk members and all unrelated MPQ members
remain untouched. Requires smpq (StormLib), ffmpeg with libx264, and ffprobe.
"""

from __future__ import annotations

import argparse
import json
import logging
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from fractions import Fraction
from pathlib import Path, PurePosixPath
from typing import Iterable, Sequence

LOG = logging.getLogger("prepare_ps3_fmv_mpqs")
SCHEMA = "DVLX-PS3-VDEC-1"
DEFAULT_MOVIES = ("gendata/logo.smk", "gendata/diablo1.smk")


@dataclass(frozen=True)
class MovieInfo:
    member: str
    width: int
    height: int
    fps_num: int
    fps_den: int
    frame_count: int
    audio_rate: int
    audio_channels: int

    @property
    def stem(self) -> str:
        return PurePosixPath(self.member.replace("\\", "/")).stem

    @property
    def parent(self) -> PurePosixPath:
        return PurePosixPath(self.member.replace("\\", "/")).parent

    def sidecar_member(self, suffix: str) -> str:
        return str(self.parent / f"{self.stem}.ps3.{suffix}")


def run(
    argv: Sequence[str],
    *,
    cwd: Path | None = None,
    check: bool = True,
    capture: bool = False,
) -> subprocess.CompletedProcess[str]:
    LOG.debug("$ %s", " ".join(str(part) for part in argv))
    result = subprocess.run(
        [str(part) for part in argv],
        cwd=cwd,
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.PIPE if capture else None,
    )
    if check and result.returncode != 0:
        detail = (result.stderr or "").strip() or (result.stdout or "").strip()
        raise RuntimeError(f"command failed: {argv[0]} ({detail or result.returncode})")
    return result


def tool_path(name: str, explicit: str | None) -> str:
    candidate = explicit or shutil.which(name)
    if not candidate:
        raise RuntimeError(f"Required tool '{name}' was not found. Install it or pass --{name}.")
    if explicit and not Path(explicit).exists() and shutil.which(explicit) is None:
        raise RuntimeError(f"Tool does not exist: {explicit}")
    return candidate


def normalized_member(member: str) -> str:
    """Normalize an MPQ member name and reject paths that could escape a temp dir."""
    text = member.strip().replace("\\", "/")
    path = PurePosixPath(text)
    if not text or path.is_absolute() or ".." in path.parts:
        raise ValueError(f"Unsafe MPQ member path: {member!r}")
    return str(path)


def parse_smpq_member_listing(output: str) -> list[str]:
    """Pick .smk member names from SMPQ's human-readable listing."""
    found: dict[str, str] = {}
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        # Standard Diablo member paths have no whitespace. The known movie
        # paths below are still attempted by hash when no listfile is present.
        for token in re.findall(r"(?i)(?:[A-Za-z0-9_. -]+[\\/])*[A-Za-z0-9_. -]+\.smk", line):
            token = token.strip().strip('"\'')
            try:
                member = normalized_member(token)
            except ValueError:
                continue
            found.setdefault(member.casefold(), member)
    return list(found.values())


def list_movie_members(
    smpq: str, archive: Path, listfile: Path | None, explicit_movies: Iterable[str]
) -> list[str]:
    args = [smpq, "-l"]
    if listfile is not None:
        args += ["-L", str(listfile.resolve())]
    args.append(str(archive.resolve()))
    listing = run(args, check=False, capture=True)
    members = parse_smpq_member_listing(listing.stdout or "") if listing.returncode == 0 else []
    # The original Diablo intro assets are addressable by known MPQ member name
    # even if the archive does not contain an internal (listfile).
    known = (*DEFAULT_MOVIES, *explicit_movies)
    keys = {item.casefold() for item in members}
    for candidate in known:
        member = normalized_member(candidate)
        if member.casefold() not in keys:
            members.append(member)
            keys.add(member.casefold())
    return sorted(members, key=str.casefold)


def locate_extracted_member(root: Path, member: str) -> Path | None:
    rel = PurePosixPath(normalized_member(member))
    candidates = [
        root.joinpath(*rel.parts),
        root / str(rel).replace("/", "\\"),
    ]
    for path in candidates:
        try:
            resolved = path.resolve()
            resolved.relative_to(root.resolve())
        except (OSError, ValueError):
            continue
        if resolved.is_file():
            return resolved

    wanted = str(rel).casefold()
    for path in root.rglob("*"):
        if path.is_file():
            relative = path.relative_to(root).as_posix().replace("\\", "/")
            if relative.casefold() == wanted:
                return path
    return None


def extract_member(
    smpq: str, archive: Path, member: str, destination: Path, listfile: Path | None
) -> Path | None:
    destination.mkdir(parents=True, exist_ok=True)
    spellings = (member.replace("/", "\\"), member.replace("\\", "/"))
    for spelling in dict.fromkeys(spellings):
        # Some SMPQ builds return success even when a requested member is absent,
        # so check for the extracted file rather than trusting only the exit code.
        args = [smpq, "-x"]
        if listfile is not None:
            args += ["-L", str(listfile.resolve())]
        args += [str(archive.resolve()), spelling]
        result = run(args, cwd=destination, check=False, capture=True)
        extracted = locate_extracted_member(destination, member)
        if extracted is not None:
            return extracted
        if result.returncode != 0:
            LOG.debug("Could not extract %s from %s: %s", member, archive.name, (result.stderr or "").strip())
    return None


def ffprobe_movie(ffprobe: str, source: Path) -> MovieInfo:
    result = run(
        [
            ffprobe, "-v", "error", "-count_frames",
            "-show_entries",
            "stream=codec_type,width,height,avg_frame_rate,r_frame_rate,nb_frames,nb_read_frames,sample_rate,channels",
            "-of", "json", str(source),
        ],
        check=True,
        capture=True,
    )
    try:
        streams = json.loads(result.stdout or "{}").get("streams", [])
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"ffprobe returned invalid JSON for {source}") from exc
    video = next((stream for stream in streams if stream.get("codec_type") == "video"), None)
    audio = next((stream for stream in streams if stream.get("codec_type") == "audio"), None)
    if video is None:
        raise RuntimeError(f"No video stream found in {source}")
    try:
        rate = Fraction(video.get("avg_frame_rate") or video.get("r_frame_rate") or "0/1")
    except (ValueError, ZeroDivisionError):
        rate = Fraction(0, 1)
    if rate <= 0:
        raise RuntimeError(f"Invalid video frame rate in {source}")
    try:
        frame_count = int(video.get("nb_read_frames") or video.get("nb_frames") or 0)
    except (TypeError, ValueError):
        frame_count = 0
    return MovieInfo(
        member="",
        width=int(video["width"]),
        height=int(video["height"]),
        fps_num=rate.numerator,
        fps_den=rate.denominator,
        frame_count=frame_count,
        audio_rate=int(audio.get("sample_rate") or 0) if audio else 0,
        audio_channels=int(audio.get("channels") or 0) if audio else 0,
    )


def count_annexb_aud_units(path: Path) -> int:
    """Count H.264 access-unit delimiter NALs in an Annex-B byte stream."""
    data = path.read_bytes()
    count = 0
    i = 0
    while i + 4 < len(data):
        if data[i:i + 4] == b"\x00\x00\x00\x01":
            nal = i + 4
        elif data[i:i + 3] == b"\x00\x00\x01":
            nal = i + 3
        else:
            i += 1
            continue
        if nal < len(data) and (data[nal] & 0x1F) == 9:
            count += 1
        i = nal + 1
    return count


def encode_movie(
    ffmpeg: str, source: Path, video_out: Path, audio_out: Path | None, info: MovieInfo
) -> MovieInfo:
    video_out.parent.mkdir(parents=True, exist_ok=True)
    gop = max(15, min(120, round(info.fps_num / info.fps_den * 2)))
    # Cell VDEC consumes H.264 access units, not Smacker packets. Emit Annex-B,
    # a delimiter NAL for each frame, repeated SPS/PPS at IDRs, no B-frames,
    # and an 8-bit 4:2:0 Constrained Baseline-compatible stream.
    run(
        [
            ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
            "-i", str(source),
            "-map", "0:v:0", "-an", "-sn", "-dn",
            "-c:v", "libx264", "-preset", "veryfast", "-tune", "zerolatency",
            "-pix_fmt", "yuv420p", "-profile:v", "baseline", "-level:v", "3.0",
            "-bf", "0", "-refs", "1", "-g", str(gop), "-keyint_min", str(gop),
            "-sc_threshold", "0", "-x264-params",
            f"aud=1:repeat-headers=1:keyint={gop}:min-keyint={gop}:scenecut=0:bframes=0:ref=1:cabac=0:open-gop=0",
            "-f", "h264", str(video_out),
        ],
        check=True,
    )
    if not video_out.is_file() or video_out.stat().st_size == 0:
        raise RuntimeError(f"FFmpeg produced no H.264 output for {source}")
    aud_count = count_annexb_aud_units(video_out)
    if aud_count == 0:
        raise RuntimeError(f"H.264 output has no AUD NALs: {video_out}")
    if info.frame_count and aud_count != info.frame_count:
        raise RuntimeError(
            f"H.264 access-unit count mismatch for {source}: "
            f"ffprobe={info.frame_count}, AUD NALs={aud_count}"
        )
    frame_count = info.frame_count or aud_count

    if audio_out is not None and info.audio_rate > 0 and info.audio_channels > 0:
        audio_out.parent.mkdir(parents=True, exist_ok=True)
        sample_rate = min(max(info.audio_rate, 8000), 48000)
        channels = min(max(info.audio_channels, 1), 2)
        run(
            [
                ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin", "-y",
                "-i", str(source),
                "-map", "0:a:0", "-vn", "-sn", "-dn",
                "-c:a", "pcm_s16be", "-ar", str(sample_rate), "-ac", str(channels),
                "-f", "s16be", str(audio_out),
            ],
            check=True,
        )
        if not audio_out.is_file() or audio_out.stat().st_size == 0:
            raise RuntimeError(f"FFmpeg produced no PCM audio output for {source}")
        audio_rate, audio_channels = sample_rate, channels
    else:
        audio_rate = audio_channels = 0

    return MovieInfo(
        member=info.member,
        width=info.width,
        height=info.height,
        fps_num=info.fps_num,
        fps_den=info.fps_den,
        frame_count=frame_count,
        audio_rate=audio_rate,
        audio_channels=audio_channels,
    )


def write_metadata(path: Path, info: MovieInfo) -> None:
    lines = [
        SCHEMA,
        f"source={normalized_member(info.member)}",
        f"width={info.width}",
        f"height={info.height}",
        f"fps_num={info.fps_num}",
        f"fps_den={info.fps_den}",
        f"frame_count={info.frame_count}",
        f"audio_rate={info.audio_rate}",
        f"audio_channels={info.audio_channels}",
        f"video={info.sidecar_member('h264')}",
        f"audio={info.sidecar_member('pcm') if info.audio_rate else 'none'}",
        "audio_format=s16be",
        "pixel_format=yuv420p",
        "bitstream=annexb-aud",
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="ascii")


def append_sidecar(
    smpq: str, output_archive: Path, source_root: Path, sidecar: Path, compression: str
) -> None:
    member_path = sidecar.relative_to(source_root).as_posix()
    run(
        [
            smpq, "-a", "-f", "-C", compression,
            str(output_archive.resolve()), member_path,
        ],
        cwd=source_root,
        check=True,
    )


def verify_sidecar(
    smpq: str, archive: Path, member: str, expected: Path, temp_root: Path, listfile: Path | None
) -> None:
    verify_dir = temp_root / "verify"
    verify_dir.mkdir(parents=True, exist_ok=True)
    extracted = extract_member(smpq, archive, member, verify_dir, listfile)
    if extracted is None:
        raise RuntimeError(f"Unable to read back {member} from rebuilt archive {archive}")
    if extracted.read_bytes() != expected.read_bytes():
        raise RuntimeError(f"Repacked member does not match generated file: {member}")


def process_archive(
    *,
    smpq: str,
    ffmpeg: str,
    ffprobe: str,
    archive: Path,
    output_dir: Path,
    listfile: Path | None,
    explicit_movies: Iterable[str],
) -> tuple[Path | None, int]:
    if not archive.is_file():
        raise RuntimeError(f"MPQ archive does not exist: {archive}")
    output_archive = output_dir / archive.name
    output_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix=f"{archive.stem}-ps3-fmv-") as temp_name:
        work_root = Path(temp_name)
        sidecars: list[tuple[str, Path, str]] = []
        movie_count = 0
        members = list_movie_members(smpq, archive, listfile, explicit_movies)
        seen: set[str] = set()

        for member in members:
            if not member.lower().endswith(".smk") or member.casefold() in seen:
                continue
            seen.add(member.casefold())
            extracted = extract_member(smpq, archive, member, work_root / "extract", listfile)
            if extracted is None:
                LOG.debug("No %s in %s", member, archive.name)
                continue
            LOG.info("Extracted %s from %s", member, archive.name)
            probed = ffprobe_movie(ffprobe, extracted)
            info = MovieInfo(
                member=normalized_member(member),
                width=probed.width,
                height=probed.height,
                fps_num=probed.fps_num,
                fps_den=probed.fps_den,
                frame_count=probed.frame_count,
                audio_rate=probed.audio_rate,
                audio_channels=probed.audio_channels,
            )

            generated_root = work_root / "generated"
            video_rel = PurePosixPath(info.sidecar_member("h264"))
            audio_rel = PurePosixPath(info.sidecar_member("pcm"))
            meta_rel = PurePosixPath(info.sidecar_member("meta"))
            video_out = generated_root.joinpath(*video_rel.parts)
            audio_out = generated_root.joinpath(*audio_rel.parts) if info.audio_rate else None
            metadata_out = generated_root.joinpath(*meta_rel.parts)

            info = encode_movie(ffmpeg, extracted, video_out, audio_out, info)
            write_metadata(metadata_out, info)
            sidecars.append((str(video_rel), video_out, "none"))
            if audio_out is not None:
                sidecars.append((str(audio_rel), audio_out, "ZLIB"))
            sidecars.append((str(meta_rel), metadata_out, "ZLIB"))
            movie_count += 1
            LOG.info(
                "Converted %s: %dx%d at %d/%d fps, %d access units, audio=%s",
                member, info.width, info.height, info.fps_num, info.fps_den,
                info.frame_count,
                f"{info.audio_rate} Hz/{info.audio_channels} ch" if info.audio_rate else "none",
            )

        if not sidecars:
            return None, 0
        if output_archive.resolve() == archive.resolve():
            raise RuntimeError("Output MPQ path must not overwrite the original archive")

        shutil.copy2(archive, output_archive)
        for _member, sidecar, compression in sidecars:
            append_sidecar(smpq, output_archive, work_root / "generated", sidecar, compression)
        for member, sidecar, _compression in sidecars:
            verify_sidecar(smpq, output_archive, member, sidecar, work_root, listfile)

        LOG.info("Created %s with %d PS3 FMV movie(s)", output_archive, movie_count)
        return output_archive, movie_count


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Extract Smacker FMVs from user-supplied Diablo MPQs, convert video "
            "to PS3 Cell-VDEC H.264, and append the converted sidecars to copies "
            "of the original MPQs. Original archives are never modified."
        )
    )
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--mpq", action="append", type=Path, help="MPQ archive to process (repeatable)")
    source.add_argument("--mpq-dir", type=Path, help="Recursively process every .mpq/.MPQ under this directory")
    parser.add_argument("--output-dir", type=Path, default=Path("ps3-fmv-mpqs"), help="Directory for converted MPQ copies")
    parser.add_argument("--listfile", type=Path, help="Optional StormLib/SMPQ listfile for discovering additional .smk members")
    parser.add_argument("--movie", action="append", default=[], help="Additional MPQ member to convert; repeatable")
    parser.add_argument("--smpq", help="Path/name of SMPQ executable (default: PATH lookup)")
    parser.add_argument("--ffmpeg", help="Path/name of FFmpeg executable (default: PATH lookup)")
    parser.add_argument("--ffprobe", help="Path/name of FFprobe executable (default: PATH lookup)")
    parser.add_argument("-v", "--verbose", action="store_true", help="Show commands and detailed extraction diagnostics")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(levelname)s: %(message)s",
    )
    try:
        smpq = tool_path("smpq", args.smpq)
        ffmpeg = tool_path("ffmpeg", args.ffmpeg)
        ffprobe = tool_path("ffprobe", args.ffprobe)
        if args.listfile is not None and not args.listfile.is_file():
            raise RuntimeError(f"Listfile does not exist: {args.listfile}")
        output_dir = args.output_dir.resolve()
        if args.mpq is not None:
            archives = [path.resolve() for path in args.mpq]
        else:
            if not args.mpq_dir.is_dir():
                raise RuntimeError(f"MPQ directory does not exist: {args.mpq_dir}")
            source_dir = args.mpq_dir.resolve()
            archives = sorted(
                (
                    path.resolve()
                    for path in source_dir.rglob("*")
                    if path.is_file()
                    and path.suffix.lower() == ".mpq"
                    and not path.resolve().is_relative_to(output_dir)
                ),
                key=lambda path: str(path).casefold(),
            )
        if not archives:
            raise RuntimeError("No MPQ archives found")
        output_names: set[str] = set()
        processed: list[Path] = []
        total_movies = 0
        for archive in archives:
            name_key = archive.name.casefold()
            if name_key in output_names:
                raise RuntimeError(
                    f"Multiple source archives have the same filename ({archive.name}); "
                    "process them in separate runs or give them unique names."
                )
            output_names.add(name_key)
            output, count = process_archive(
                smpq=smpq,
                ffmpeg=ffmpeg,
                ffprobe=ffprobe,
                archive=archive,
                output_dir=output_dir,
                listfile=args.listfile,
                explicit_movies=args.movie,
            )
            if output is not None:
                processed.append(output)
                total_movies += count

        if total_movies == 0:
            raise RuntimeError(
                "No Smacker movies were found. Check that the MPQs are the original game data "
                "and that SMPQ can read the requested members. A listfile can help discover "
                "additional members (pass --listfile)."
            )
        print("\nPS3 FMV conversion complete")
        print(f"Movies converted: {total_movies}")
        print(f"Converted MPQ archives: {len(processed)}")
        for path in processed:
            print(f"  {path}")
        print("Copy the converted MPQ files over the corresponding game MPQs on the PS3.")
        print("The original source MPQs were not modified.")
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        LOG.error("%s", exc)
        return 1


if __name__ == "__main__":
    sys.exit(main())
