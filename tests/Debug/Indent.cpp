/*
TEST_F(NeovimOracleDebug, DISABLED_InvestigateBackspaceAutoindent) {
  // Test: Does <BS> delete autoindent space after <CR>?
  // Line " dc f" has 1 space indent. After A<CR>, autoindent copies it.
  // Can <BS> remove that autoindent space?

  cerr << "=== Full sequence with <BS> ===" << endl;
  auto tracer2 = makeTracer({" dc f"}, 0, 0);
  tracer2.traceFullSequence("GA<CR><BS>bfa<Esc>");

  cerr << endl << "=== With <C-u> instead ===" << endl;
  auto tracer3 = makeTracer({" dc f"}, 0, 0);
  tracer3.traceFullSequence("GA<CR><C-u>bfa<Esc>");

  cerr << endl << "=== Deeper indent: 2x<BS> on 4 spaces ===" << endl;
  auto tracer4 = makeTracer({"    deep"}, 0, 0);
  tracer4.traceFullSequence("GA<CR><BS><BS>xx<Esc>");

  cerr << endl << "=== Deeper indent with <C-u> ===" << endl;
  auto tracer5 = makeTracer({"    deep"}, 0, 0);
  tracer5.traceFullSequence("GA<CR><C-u>xx<Esc>");

  // Test if \x08 (ASCII BS) behaves differently from \x7f (DEL)
  // The oracle sends \x7f for <BS>. Maybe \x08 works?
  // simulate() passes raw bytes through unchanged (only <...> tags are converted)
  cerr << endl << "=== Raw \\x08 (ASCII BS) ===" << endl;
  {
    string rawKeys = "GA\r\x08""bfa\x1b";  // \r=CR, \x08=BS, \x1b=Esc
    auto r = oracle_->simulate({" dc f"}, 0, 0, rawKeys);
    cerr << "Lines: " << r.lines << endl;
    cerr << "Cursor: (" << r.row << ", " << r.col << ")" << endl;
  }

  cerr << endl << "=== Raw \\x7f (DEL/current) ===" << endl;
  {
    string rawKeys = "GA\r\x7f""bfa\x1b";  // \r=CR, \x7f=DEL, \x1b=Esc
    auto r = oracle_->simulate({" dc f"}, 0, 0, rawKeys);
    cerr << "Lines: " << r.lines << endl;
    cerr << "Cursor: (" << r.row << ", " << r.col << ")" << endl;
  }
}
*/
