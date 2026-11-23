"""Rebuild the vector marker PDF. Requires opencv-python-headless and reportlab.

Run with --verify to check vector dimensions and ArUco IDs at 300 dpi
(verification also requires pymupdf and numpy).
"""
import argparse
from pathlib import Path

import cv2
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.pdfgen import canvas


OUTPUT = Path(__file__).resolve().parents[1] / "Docs/Markers/markers_aruco4x4_ID1-4.pdf"
PAGES = (
    (60, ((1, 30, 40), (2, 120, 40), (3, 30, 150), (4, 120, 150))),
    (80, ((1, 40, 45), (2, 40, 170))),
    (80, ((3, 40, 45), (4, 40, 170))),
)


def generate():
    dictionary = cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50)
    pdf = canvas.Canvas(str(OUTPUT), pagesize=A4, pageCompression=0, invariant=1)
    pdf.setTitle("VRCalibrationKit marker set")
    pdf.setAuthor("VRCalibrationKit")
    height = A4[1]

    def text(x, top, value, size):
        pdf.setFont("Helvetica", size)
        pdf.drawString(x * mm, height - top * mm, value)

    def line(x1, y1, x2, y2):
        pdf.line(x1 * mm, height - y1 * mm, x2 * mm, height - y2 * mm)

    for size, markers in PAGES:
        text(15, 14, f"VRCalibrationKit marker set - ArUco DICT_4X4_50, {size} mm", 10)
        pdf.setLineWidth(0.3)
        pdf.setStrokeColorRGB(0, 0, 0)
        line(15, 22, 115, 22)
        line(15, 20, 15, 24)
        line(115, 20, 115, 24)
        text(118, 23, "100 mm - measure after print. Print at 100% / Actual size (no fit to page)", 7)
        cell = size / 6
        for marker_id, x, top in markers:
            pattern = cv2.aruco.generateImageMarker(dictionary, marker_id, 6)
            pdf.setFillColorRGB(0, 0, 0)
            for row in range(6):
                for col in range(6):
                    if pattern[row, col] == 0:
                        pdf.rect((x + col * cell) * mm, height - (top + (row + 1) * cell) * mm,
                                 cell * mm, cell * mm, stroke=0, fill=1)
            left, upper = x - cell, top - cell
            edge = size + 2 * cell
            pdf.setStrokeColorRGB(0.5, 0.5, 0.5)
            pdf.setDash(2, 2)
            pdf.rect(left * mm, height - (upper + edge) * mm, edge * mm, edge * mm, stroke=1, fill=0)
            pdf.setDash()
            for y in (upper, upper + edge):
                line(x + size / 2, y - 3, x + size / 2, y + 3)
            for side in (left, left + edge):
                line(side - 3, top + size / 2, side + 3, top + size / 2)
            text(left, top + size + cell + 6, f"ArUco 4x4_50  ID {marker_id}  = align point {marker_id}", 9)
            text(left, top + size + cell + 10.5, f"black square {size:.1f} mm (cell {cell:.2f} mm)", 7)
        pdf.showPage()
    pdf.save()


def verify():
    import numpy as np
    import pymupdf

    document = pymupdf.open(OUTPUT)
    assert len(document) == len(PAGES)
    detector = cv2.aruco.ArucoDetector(cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_4X4_50))
    for index, (page, (size, markers)) in enumerate(zip(document, PAGES), 1):
        assert f"VRCalibrationKit marker set - ArUco DICT_4X4_50, {size} mm" in page.get_text()
        drawings = page.get_drawings()
        reference = drawings[0]["rect"]
        assert abs(reference.width / mm - 100) < 0.001
        for marker_id, x, top in markers:
            cells = [d["rect"] for d in drawings if d["type"] == "f"
                     and x * mm - 0.01 <= d["rect"].x0 < (x + size) * mm - 0.01
                     and top * mm - 0.01 <= d["rect"].y0 < (top + size) * mm - 0.01]
            assert cells, (index, marker_id)
            width = (max(r.x1 for r in cells) - min(r.x0 for r in cells)) / mm
            height = (max(r.y1 for r in cells) - min(r.y0 for r in cells)) / mm
            assert abs(width - size) < 0.001 and abs(height - size) < 0.001, (width, height)
        pixmap = page.get_pixmap(dpi=300, colorspace=pymupdf.csGRAY, alpha=False)
        pixels = np.frombuffer(pixmap.samples, dtype=np.uint8).reshape(pixmap.height, pixmap.width)
        corners, ids, _ = detector.detectMarkers(pixels)
        assert ids is not None and sorted(ids.flatten()) == sorted(m[0] for m in markers)
        for corner in corners:
            sides = np.linalg.norm(np.roll(corner[0], -1, axis=0) - corner[0], axis=1) * 25.4 / 300
            assert np.all(np.abs(sides - size) < 0.15), sides
        print(f"Page {index}: IDs {sorted(ids.flatten().tolist())}, vector {size:.1f} x {size:.1f} mm, "
              "300 dpi sides within 0.15 mm, reference 100.0 mm")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    generate()
    if args.verify:
        verify()
