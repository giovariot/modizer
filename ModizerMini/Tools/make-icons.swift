#!/usr/bin/env swift
//
//  make-icons.swift
//
//  Draws the ModizerMini icons with Core Graphics: a monochrome squircle with
//  the slanted "M" mark from Modizer. Each ribbon is a parallelogram with a
//  beveled foot, and all three are parallel.
//
//  Usage: swift Tools/make-icons.swift <output-directory>
//

import AppKit
import CoreGraphics
import Foundation

let designSize: CGFloat = 1024

// MARK: - colours

/// Modizer magenta (#E5277E) — the single colour of the icon.
func brand(alpha: CGFloat = 1) -> CGColor {
    CGColor(red: 0xE5 / 255.0, green: 0x27 / 255.0, blue: 0x7E / 255.0, alpha: alpha)
}
func ink(alpha: CGFloat = 1) -> CGColor { CGColor(red: 1, green: 1, blue: 1, alpha: alpha) }

// MARK: - helpers

func makeContext(_ pixels: Int) -> CGContext {
    let space = CGColorSpaceCreateDeviceRGB()
    let context = CGContext(data: nil,
                            width: pixels, height: pixels,
                            bitsPerComponent: 8, bytesPerRow: 0,
                            space: space,
                            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    context.setAllowsAntialiasing(true)
    context.interpolationQuality = .high
    let scale = CGFloat(pixels) / designSize
    context.scaleBy(x: scale, y: scale)
    return context
}

func write(_ context: CGContext, to url: URL) {
    guard let image = context.makeImage() else { return }
    let rep = NSBitmapImageRep(cgImage: image)
    guard let data = rep.representation(using: .png, properties: [:]) else { return }
    try? data.write(to: url)
}

// MARK: - the Modizer "M" mark (three parallel beveled ribbons)

/// One slanted ribbon: a parallelogram leaning to the right whose foot is cut
/// at an angle (the bevel), so the bottom-left drops lower than the right.
func white(_ a: CGFloat) -> CGColor { CGColor(red: 1, green: 1, blue: 1, alpha: a) }
func black(_ a: CGFloat) -> CGColor { CGColor(red: 0, green: 0, blue: 0, alpha: a) }

/// Path of one slanted ribbon: a parallelogram leaning to the right, with the
/// foot cut at an angle (the bevel) so the bottom-left drops lower than the
/// bottom-right. Note: Core Graphics has y pointing up, so @p yTop > @p yBottom.
func ribbonPath(x: CGFloat, yTop: CGFloat, yBottom: CGFloat,
                width: CGFloat, slant: CGFloat, bevel: CGFloat) -> CGPath {
    let path = CGMutablePath()
    path.move(to: CGPoint(x: x, y: yBottom))                       // low, pointed foot
    path.addLine(to: CGPoint(x: x + width, y: yBottom + bevel))    // bevel rises to the right
    path.addLine(to: CGPoint(x: x + width + slant, y: yTop))       // top-right
    path.addLine(to: CGPoint(x: x + slant, y: yTop))               // top-left
    path.closeSubpath()
    return path
}

/// Renders one element as a piece of Liquid Glass: a soft cast shadow, a
/// translucent body with a specular sheen, and an illuminated rim that is
/// brighter along the top edge (directional light).
func glassRibbon(_ context: CGContext, path: CGPath, highlight: CGPath?) {
    let bounds = path.boundingBox
    let space = CGColorSpaceCreateDeviceRGB()

    // soft cast shadow + base body
    context.saveGState()
    context.setShadow(offset: CGSize(width: 0, height: -5), blur: 12, color: black(0.22))
    context.addPath(path)
    context.setFillColor(white(0.86))
    context.fillPath()
    context.restoreGState()

    // body: translucent, brighter toward the top
    context.saveGState()
    context.addPath(path)
    context.clip()
    if let body = CGGradient(colorsSpace: space,
                             colors: [white(0.98), white(0.74)] as CFArray,
                             locations: [0, 1]) {
        context.drawLinearGradient(body,
                                   start: CGPoint(x: bounds.minX, y: bounds.maxY),
                                   end: CGPoint(x: bounds.minX, y: bounds.minY),
                                   options: [])
    }
    // specular sheen across the upper part
    if let sheen = CGGradient(colorsSpace: space,
                              colors: [white(0.55), white(0.0)] as CFArray,
                              locations: [0, 1]) {
        context.drawLinearGradient(sheen,
                                   start: CGPoint(x: bounds.minX, y: bounds.maxY - bounds.height * 0.12),
                                   end: CGPoint(x: bounds.maxX, y: bounds.maxY - bounds.height * 0.62),
                                   options: [])
    }
    context.restoreGState()

    // rim: dimmer all around…
    context.addPath(path)
    context.setStrokeColor(white(0.62))
    context.setLineWidth(6)
    context.setLineJoin(.round)
    context.strokePath()

    // …brighter along the top edge.
    if let highlight = highlight {
        context.addPath(highlight)
        context.setStrokeColor(white(1.0))
        context.setLineWidth(7)
        context.setLineCap(.round)
        context.strokePath()
    }
}

/// Three parallel glass ribbons forming Modizer's mark silhouette.
func modizerMark(_ context: CGContext, color: CGColor, glass: Bool) {
    let slant: CGFloat = 116
    let width: CGFloat = 118
    let yTop: CGFloat = 712
    let yBottom: CGFloat = 318
    let bevel: CGFloat = 58
    let step: CGFloat = 178
    let startX: CGFloat = 214
    for i in 0..<3 {
        let x = startX + CGFloat(i) * step
        let path = ribbonPath(x: x, yTop: yTop, yBottom: yBottom,
                              width: width, slant: slant, bevel: bevel)
        if glass {
            let topEdge = CGMutablePath()
            topEdge.move(to: CGPoint(x: x + slant, y: yTop))
            topEdge.addLine(to: CGPoint(x: x + width + slant, y: yTop))
            glassRibbon(context, path: path, highlight: topEdge)
        } else {
            context.addPath(path)
            context.setFillColor(color)
            context.fillPath()
        }
    }
}

// MARK: - app icon

func drawAppIcon(_ pixels: Int) -> CGContext {
    let context = makeContext(pixels)

    let inset: CGFloat = 82
    let background = CGRect(x: inset, y: inset,
                            width: designSize - inset * 2, height: designSize - inset * 2)
    let radius = background.width * 0.2237

    context.addPath(CGPath(roundedRect: background, cornerWidth: radius, cornerHeight: radius, transform: nil))
    context.setFillColor(brand())
    context.fillPath()

    modizerMark(context, color: ink(), glass: true)

    return context
}

// MARK: - document icon

func drawDocIcon(_ pixels: Int) -> CGContext {
    let context = makeContext(pixels)

    let page = CGRect(x: 212, y: 120, width: 600, height: 784)
    let radius: CGFloat = 74

    context.saveGState()
    context.setShadow(offset: CGSize(width: 0, height: -14), blur: 30, color: CGColor(red: 0, green: 0, blue: 0, alpha: 0.25))
    context.addPath(CGPath(roundedRect: page, cornerWidth: radius, cornerHeight: radius, transform: nil))
    context.setFillColor(CGColor(red: 1, green: 1, blue: 1, alpha: 1))
    context.fillPath()
    context.restoreGState()

    // the same mark, in the brand colour, centred in the page
    context.saveGState()
    context.translateBy(x: 512, y: 640)
    context.scaleBy(x: 0.56, y: 0.56)
    context.translateBy(x: -512, y: -512)
    modizerMark(context, color: brand(), glass: false)
    context.restoreGState()

    context.setFillColor(CGColor(red: 0x1B / 255.0, green: 0x20 / 255.0, blue: 0x30 / 255.0, alpha: 0.18))
    for (index, width) in [340, 260, 300].enumerated() {
        let rect = CGRect(x: page.minX + 90, y: 300 - CGFloat(index) * 52, width: CGFloat(width), height: 22)
        context.addPath(CGPath(roundedRect: rect, cornerWidth: 11, cornerHeight: 11, transform: nil))
        context.fillPath()
    }

    return context
}

// MARK: - main

let arguments = CommandLine.arguments
guard arguments.count >= 2 else {
    FileHandle.standardError.write("usage: make-icons.swift <output-directory>\n".data(using: .utf8)!)
    exit(1)
}
let output = URL(fileURLWithPath: arguments[1])
try? FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)

for size in [16, 32, 64, 128, 256, 512, 1024] {
    write(drawAppIcon(size), to: output.appendingPathComponent("icon_\(size).png"))
}
for size in [16, 32, 64, 128, 256, 512, 1024] {
    write(drawDocIcon(size), to: output.appendingPathComponent("doc_\(size).png"))
}

print("wrote icons to \(output.path)")
