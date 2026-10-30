import Foundation
import CoreGraphics

// usage: winclick <owner> bounds | click <fx> <fy> [holdMs] | move <fx> <fy> | key <keycode> <downMs>
let args = CommandLine.arguments
guard args.count >= 3 else { print("usage: winclick <owner> bounds|click|move|key ..."); exit(2) }
let owner = args[1]
func bounds() -> CGRect? {
    let list = CGWindowListCopyWindowInfo([.optionOnScreenOnly], kCGNullWindowID) as? [[String: Any]] ?? []
    for w in list where (w[kCGWindowOwnerName as String] as? String) == owner {
        if let b = w[kCGWindowBounds as String] as? [String: CGFloat], (b["Height"] ?? 0) > 100 {
            return CGRect(x: b["X"]!, y: b["Y"]!, width: b["Width"]!, height: b["Height"]!)
        }
    }
    return nil
}
guard let r = bounds() else { print("no window"); exit(1) }
let titleBar: CGFloat = 32
func point(_ fx: Double, _ fy: Double) -> CGPoint { CGPoint(x: r.minX + r.width * fx, y: r.minY + titleBar + (r.height - titleBar) * fy) }
switch args[2] {
case "bounds": print("\(r.minX) \(r.minY) \(r.width) \(r.height)")
case "move", "click":
    let p = point(Double(args[3])!, Double(args[4])!)
    CGWarpMouseCursorPosition(p)
    CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: p, mouseButton: .left)?.post(tap: .cghidEventTap)
    usleep(300_000)
    if args[2] == "click" {
        let hold = UInt32(args.count > 5 ? Int(args[5])! : 150) * 1000
        CGEvent(mouseEventSource: nil, mouseType: .leftMouseDown, mouseCursorPosition: p, mouseButton: .left)?.post(tap: .cghidEventTap)
        usleep(hold)
        CGEvent(mouseEventSource: nil, mouseType: .leftMouseUp, mouseCursorPosition: p, mouseButton: .left)?.post(tap: .cghidEventTap)
    }
    print("ok \(p)")
case "key":
    let code = CGKeyCode(UInt16(args[3])!)
    let ms = UInt32(args[4])! * 1000
    CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: true)?.post(tap: .cghidEventTap)
    usleep(ms)
    CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: false)?.post(tap: .cghidEventTap)
    print("ok key")
default: print("bad cmd")
}
