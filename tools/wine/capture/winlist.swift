// Lists on-screen windows whose owner name contains argv[1] (case-insensitive): id owner | title | bounds
import CoreGraphics
import Foundation
let needle = (CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "wine").lowercased()
let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as! [[String: Any]]
for w in list {
  let owner = w[kCGWindowOwnerName as String] as? String ?? ""
  let name = w[kCGWindowName as String] as? String ?? ""
  let id = w[kCGWindowNumber as String] as? Int ?? 0
  let b = w[kCGWindowBounds as String] as? [String: Any] ?? [:]
  let layer = w[kCGWindowLayer as String] as? Int ?? -1
  if owner.lowercased().contains(needle) || name.lowercased().contains(needle) { print(id, owner, "|", name, "| layer", layer, b) }
}
