-- re:Lain Android Test HUD
-- Smoke test only: draws a small panel over the game and can be hidden from F1 > Mods.

local visible = true

lain.on("draw", function()
  if not visible then return end
  lain.ui.draw_rect(0.012, 0.018, 0.37, 0.105, 0.015, 0.045, 0.065, 0.78)
  lain.ui.draw_text(0.025, 0.028, "re:Lain MOD TEST: ACTIVE", 0.30, 0.92, 1.0, 11)
  lain.ui.draw_text(0.025, 0.070, "Lua mod loaded successfully", 0.88, 0.94, 1.0, 8)
end)

lain.on("menu", function()
  visible = lain.ui.checkbox("Show re:Lain Android test HUD", visible)
end)
