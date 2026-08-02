#!/usr/bin/python3
#
# Render Rockpool's current-model watch frames from the public Core Devices
# external CAD models.  This is a source-maintenance tool, not part of the
# Sailfish package build.

import argparse
import struct
from pathlib import Path

import numpy
from PIL import Image, ImageDraw


TRIANGLE_DTYPE = numpy.dtype([
    ("normal", "<f4", (3,)),
    ("vertices", "<f4", (3, 3)),
    ("attribute", "<u2"),
])

PEBBLE_2_DUO_VARIANTS = (
    ("pebble-2-duo-black.png", (43, 44, 46), (34, 35, 37)),
    ("pebble-2-duo-white.png", (245, 245, 241), (238, 238, 234)),
)

TIME_2_VARIANTS = (
    ("pebble-time-2-black-gray.png", (47, 49, 52), (77, 78, 81)),
    ("pebble-time-2-black-red.png", (47, 49, 52), (143, 32, 38)),
    ("pebble-time-2-silver-blue.png", (174, 179, 184), (31, 60, 100)),
    ("pebble-time-2-silver-gray.png", (174, 179, 184), (77, 78, 81)),
)

ROUND_2_20MM_VARIANTS = (
    ("pebble-round-2-black.png", (43, 44, 46), (34, 35, 37)),
    ("pebble-round-2-silver.png", (181, 185, 188), (218, 218, 213)),
)

ROUND_2_14MM_VARIANTS = (
    ("pebble-round-2-gold.png", (187, 132, 101), (27, 53, 91)),
    ("pebble-round-2-silver-14mm.png", (181, 185, 188), (218, 218, 213)),
)


def read_binary_stl(path):
    with path.open("rb") as stream:
        stream.seek(80)
        triangle_count = struct.unpack("<I", stream.read(4))[0]
        triangles = numpy.fromfile(
            stream, dtype=TRIANGLE_DTYPE, count=triangle_count
        )

    if len(triangles) != triangle_count:
        raise ValueError(f"{path}: truncated binary STL")
    return triangles


def shaded_colors(normals, heights, base_color):
    lengths = numpy.linalg.norm(normals, axis=1)
    lengths[lengths == 0] = 1
    unit_normals = normals / lengths[:, None]
    light = numpy.array((-0.42, -0.50, 0.76))
    light /= numpy.linalg.norm(light)
    diffuse = numpy.clip(unit_normals @ light, 0, 1)
    height_range = numpy.ptp(heights)
    relative_height = (
        (heights - heights.min()) / height_range
        if height_range
        else numpy.zeros_like(heights)
    )
    intensity = numpy.clip(0.48 + 0.38 * diffuse + 0.14 * relative_height,
                           0.32, 1.08)
    colors = numpy.asarray(base_color)[None, :] * intensity[:, None]
    return numpy.clip(colors, 0, 255).astype(numpy.uint8)


def draw_strap(image, center_x, width, color):
    draw = ImageDraw.Draw(image, "RGBA")
    left = int(round(center_x - width / 2))
    right = int(round(center_x + width / 2))
    radius = max(3, int(round(width * 0.055)))
    draw.rounded_rectangle(
        (left, -radius, right, image.height + radius),
        radius=radius,
        fill=(*color, 255),
    )

    highlight = tuple(min(255, int(channel * 1.16 + 7)) for channel in color)
    shadow = tuple(max(0, int(channel * 0.68)) for channel in color)
    edge = max(2, int(round(width * 0.025)))
    draw.rounded_rectangle(
        (left + edge, -radius, right - edge, image.height + radius),
        radius=radius,
        outline=(*highlight, 105),
        width=edge,
    )
    draw.line(
        (right - edge, 0, right - edge, image.height),
        fill=(*shadow, 115),
        width=edge,
    )


def render_variant(stl_path, output_path, *, canvas_size, pixels_per_mm,
                   case_color, strap_color, strap_width_mm, screen_size,
                   screen_radius, model_center_xy=None, bezel_size=None,
                   bezel_radius=0, bezel_color=(18, 19, 20)):
    triangles = read_binary_stl(stl_path)
    vertices = triangles["vertices"].astype(numpy.float64)
    all_vertices = vertices.reshape(-1, 3)
    bounds_min = all_vertices.min(axis=0)
    bounds_max = all_vertices.max(axis=0)
    model_center = (bounds_min + bounds_max) / 2
    if model_center_xy is not None:
        model_center[:2] = model_center_xy

    antialias = 3
    width, height = canvas_size
    render_width = width * antialias
    render_height = height * antialias
    scale = pixels_per_mm * antialias

    image = Image.new("RGBA", (render_width, render_height), (0, 0, 0, 0))
    draw_strap(
        image,
        render_width / 2,
        strap_width_mm * scale,
        strap_color,
    )

    projected = numpy.empty((len(vertices), 3, 2), dtype=numpy.float64)
    projected[:, :, 0] = (
        (vertices[:, :, 0] - model_center[0]) * scale
        + render_width / 2
    )
    projected[:, :, 1] = (
        -(vertices[:, :, 1] - model_center[1]) * scale
        + render_height / 2
    )

    mean_heights = vertices[:, :, 2].mean(axis=1)
    order = numpy.argsort(mean_heights)
    colors = shaded_colors(
        triangles["normal"].astype(numpy.float64),
        mean_heights,
        case_color,
    )

    draw = ImageDraw.Draw(image, "RGBA")
    for triangle_index in order:
        points = [
            (round(point[0]), round(point[1]))
            for point in projected[triangle_index]
        ]
        color = colors[triangle_index]
        draw.polygon(points, fill=(int(color[0]), int(color[1]),
                                   int(color[2]), 255))

    if bezel_size is not None:
        bezel_width = bezel_size[0] * antialias
        bezel_height = bezel_size[1] * antialias
        bezel_box = (
            round((render_width - bezel_width) / 2),
            round((render_height - bezel_height) / 2),
            round((render_width + bezel_width) / 2),
            round((render_height + bezel_height) / 2),
        )
        draw.rounded_rectangle(
            bezel_box,
            radius=bezel_radius * antialias,
            fill=(*bezel_color, 255),
        )

    screen_width = screen_size[0] * antialias
    screen_height = screen_size[1] * antialias
    screen_box = (
        round((render_width - screen_width) / 2),
        round((render_height - screen_height) / 2),
        round((render_width + screen_width) / 2),
        round((render_height + screen_height) / 2),
    )
    if screen_width == screen_height:
        draw.ellipse(screen_box, fill=(0, 0, 0, 0))
    else:
        draw.rounded_rectangle(
            screen_box,
            radius=screen_radius * antialias,
            fill=(0, 0, 0, 0),
        )

    image = image.resize(canvas_size, Image.Resampling.LANCZOS)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    image.save(output_path, optimize=True)


def render_round_2_variants(stl_path, variants, strap_width_mm,
                            output_directory):
    for filename, case_color, strap_color in variants:
        render_variant(
            stl_path,
            output_directory / filename,
            canvas_size=(344, 352),
            pixels_per_mm=260 / (1.3 * 25.4),
            case_color=case_color,
            strap_color=strap_color,
            strap_width_mm=strap_width_mm,
            screen_size=(260, 260),
            screen_radius=130,
        )


def render_all(pebble_2_duo_stl, time_2_stl, round_2_14mm_stl,
               round_2_20mm_stl, output_directory):
    for filename, case_color, strap_color in PEBBLE_2_DUO_VARIANTS:
        render_variant(
            pebble_2_duo_stl,
            output_directory / filename,
            canvas_size=(236, 372),
            pixels_per_mm=175 / 25.4,
            case_color=case_color,
            strap_color=strap_color,
            strap_width_mm=22,
            screen_size=(144, 168),
            screen_radius=4,
            bezel_size=(190, 216),
            bezel_radius=16,
        )

    for filename, case_color, strap_color in TIME_2_VARIANTS:
        render_variant(
            time_2_stl,
            output_directory / filename,
            canvas_size=(294, 368),
            pixels_per_mm=7.8125,
            case_color=case_color,
            strap_color=strap_color,
            strap_width_mm=22,
            screen_size=(200, 228),
            screen_radius=20,
            model_center_xy=(127.963, 128.000),
        )

    render_round_2_variants(
        round_2_14mm_stl,
        ROUND_2_14MM_VARIANTS,
        14,
        output_directory,
    )
    render_round_2_variants(
        round_2_20mm_stl,
        ROUND_2_20MM_VARIANTS,
        20,
        output_directory,
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("pebble_2_duo_stl", type=Path)
    parser.add_argument("time_2_stl", type=Path)
    parser.add_argument("round_2_14mm_stl", type=Path)
    parser.add_argument("round_2_20mm_stl", type=Path)
    parser.add_argument(
        "--output-directory",
        type=Path,
        default=Path(__file__).resolve().parent,
    )
    args = parser.parse_args()

    render_all(
        args.pebble_2_duo_stl,
        args.time_2_stl,
        args.round_2_14mm_stl,
        args.round_2_20mm_stl,
        args.output_directory,
    )


if __name__ == "__main__":
    main()
