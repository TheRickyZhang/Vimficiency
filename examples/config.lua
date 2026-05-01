return {
  dir = "~/Projects/vimficiency",
  name = "vimficiency",
  lazy = false,
  config = function()

    require("vimficiency").setup({
      ----------------------------------------------------------------
      -- Lua-side knobs
      ----------------------------------------------------------------

      -- Optimizer/search
      RESULTS_CALCULATED = 20,
      RESULTS_SAVED = 5,
      SLICE_PADDING = 5,
      SLICE_EXPAND_TO_PARAGRAPH = false,
      MAX_SEARCH_LINES = 500,

      -- Recall/session safety
      KEY_SESSION_CAPACITY = 200,
      MAX_RETENTION_SECONDS = 120,
      MANUAL_IDLE_TIMEOUT_SECONDS = 300,
      SNAP_LOOKBACK_KEYS = 20,

      -- Watch: omit entirely to disable
      watch = {
        idle = { ms = 3000 },
        cooldown_ms = 5000,
      },

      -- Auto-suggest: omit triggers you do not want.
      -- If a trigger is present, specify the whole trigger.
      -- auto_suggest = {
      --   cooldown_ms = 5000,
      --
      --   idle = {
      --     ms = 3000,
      --     window = "3s",
      --   },
      --
      --   -- keys = {
      --   --   every = 50,
      --   -- },
      --
      --   -- cost = {
      --   --   m = 1.5,
      --   --   b = 2.0,
      --   --   ms = 300,
      --   --   window = "100",
      --   -- },
      -- },


      ----------------------------------------------------------------
      -- C++ / effort-model knobs
      ----------------------------------------------------------------

      -- Current enum in C++:
      --   0 = NONE
      --   1 = UNIFORM
      --   2 = QWERTY
      --   3 = COLEMAK_DH
      default_keyboard = 1,

      -- Optional runtime override; setup() auto-fills from vim.o.shiftwidth
      -- if you omit it.
      -- shiftwidth = 2,

      -- Advanced slice knob passed to C++.
      -- slice_buffer_amount = 5,

      -- weights = {
      --   keyWeight = 1.0,
      --   sameFingerWeight = 0.0,
      --   sameKeyWeight = 0.0,
      --   altHandWeight = 0.0,
      --   goodRollWeight = 0.0,
      --   badRollWeight = 0.0,
      -- },

      -- Override specific keys by ffi.Key index.
      -- hand/finger values come from ffi.Hand / ffi.Finger.
      -- local ffi = require("vimficiency.ffi")
      -- keys = {
      --   [ffi.Key.Space] = {
      --     hand = ffi.Hand.Left,
      --     finger = ffi.Finger.Lt,
      --     cost = 0.2,
      --   },
      -- },

      -- If count_penalty_overrides is present and
      -- use_count_penalty_overrides is omitted, Lua enables it for you.
      -- use_count_penalty_overrides = true,
      -- count_penalty_overrides = {
      --   MotionWord = {
      --     base = 2.0,
      --     count_slope = 0.8,
      --   },
      --
      --   EditLine = {
      --     base = 1.5,
      --     span_slope = 0.2,
      --   },
      --
      --   Join = {
      --     base = 3.0,
      --   },
      -- },

      ----------------------------------------------------------------
      -- Per-feature UI preferences
      ----------------------------------------------------------------
      -- Both sections also togglable at runtime (explore via `gs`,
      -- play via `:Vimfy play_settings`); runtime toggles auto-
      -- persist to sidecar JSON under stdpath("data")/vimficiency.
      -- Declare here to set the cold-start baseline (what a fresh
      -- install / new machine starts with); the sidecar layers on
      -- top and wins for any key you've toggled.

      -- explore = {
      --   display_mode = "above",                       -- "off" | "highlight" | "inplace" | "above" | "below"
      --   staged_mode = false,                          -- false → flat header; true → sectioned-by-stage
      --   recommendation_count = 5,                     -- how many recs to surface; 1..10
      --   allow_multiple_motions_per_position = false,  -- false → motion recs dedup by landing cell
      --   allow_multiple_edits_per_position = false,    -- false → edit recs dedup by command shape
      --   show_user_typed = true,                       -- include the user's captured typed sequence
      --   show_result_count = 1,                        -- how many `optimal_results` to display alongside Explored
      -- },

      -- play = {
      --   include_user_sequence = true,                 -- show the user's typed sequence alongside the optimal(s)
      --   default_result_count = 1,                     -- how many optimal_results to show by default; 1..8
      -- },
    })

    local vimfy = require("vimficiency")

    local function cmd_map(lhs, subcmd, desc)
      vimfy.map("n", lhs, function()
        vim.cmd("Vimfy " .. subcmd)
      end, { desc = desc })
    end

    -- create bindings
    local function prompt_map(lhs, subcmd, desc)
      vimfy.map("n", lhs, function()
        vim.ui.input({ prompt = subcmd .. " alias: "}, function(name)
          if name and name ~= "" then
            vim.cmd("Vimfy " .. subcmd .. " " .. name)
          end
        end)
      end, { desc = desc })
    end


    cmd_map("<leader>vl", "list", "Vimfy list")

    prompt_map("<leader>vb", "start", "Start mark")
    prompt_map("<leader>vf", "finish", "Finish mark")
    prompt_map("<leader>vw", "watch", "Start watch")
    prompt_map("<leader>vr", "recall", "Finish recall")

    prompt_map("<leader>vs", "save",  "@")
    prompt_map("<leader>vp", "play",  "Vimfy play") -- maybe change simulate -> replay
    prompt_map("<leader>ve", "explore", "Explore mark")

    vimfy.map("n", "<leader>vc", "reload")

  end,
}
