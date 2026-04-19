/*
TEST_F(NeovimOracleDebug, DISABLED_InvestigateSentenceMotion) {
  cerr << "=== Understanding d( sentence motion ===" << endl;

  // From the failing test
  Lines source = {"d..,b,b", "eafecf  ", ".f ,b,f"};
  cerr << "Source:" << endl;
  for (size_t i = 0; i < source.size(); i++) {
    cerr << "  [" << i << "]: '" << source[i] << "' len=" << source[i].size() << endl;
  }

  // Test d( at various positions
  cerr << "\nd( from different positions:" << endl;
  for (int col = 0; col <= 7; col++) {
    auto r = oracle_->simulate(source, 1, col, "d(");
    cerr << "  d( at [1," << col << "]: '" << r.lines.flatten() << "' cursor=[" << r.row << "," << r.col << "]" << endl;
  }

  // What about the ( motion alone (without delete)?
  cerr << "\n( motion (cursor only) from different positions:" << endl;
  for (int col = 0; col <= 7; col++) {
    auto r = oracle_->simulate(source, 1, col, "(");
    cerr << "  ( at [1," << col << "]: cursor=[" << r.row << "," << r.col << "]" << endl;
  }

  // Test simpler case
  cerr << "\nSimpler case - 'Hello. World' on two lines:" << endl;
  Lines simple = {"Hello.", "World here."};
  cerr << "Source: '" << simple[0] << "' / '" << simple[1] << "'" << endl;

  auto r1 = oracle_->simulate(simple, 1, 0, "d(");
  cerr << "  d( at [1,0]: '" << r1.lines.flatten() << "' cursor=[" << r1.row << "," << r1.col << "]" << endl;

  auto r2 = oracle_->simulate(simple, 1, 5, "d(");
  cerr << "  d( at [1,5]: '" << r2.lines.flatten() << "' cursor=[" << r2.row << "," << r2.col << "]" << endl;

  // What about d) ?
  cerr << "\nd) from different positions on source buffer:" << endl;
  for (int line = 0; line < 3; line++) {
    auto r = oracle_->simulate(source, line, 0, "d)");
    cerr << "  d) at [" << line << ",0]: '" << r.lines.flatten() << "' cursor=[" << r.row << "," << r.col << "]" << endl;
  }

  // Is d( linewise when crossing lines?
  cerr << "\nChecking if d( is linewise:" << endl;
  Lines test = {"abc", "def", "ghi"};
  auto rtest = oracle_->simulate(test, 1, 2, "d(");
  cerr << "  'abc'/'def'/'ghi' d( at [1,2]: '" << rtest.lines.flatten() << "' cursor=[" << rtest.row << "," << rtest.col << "]" << endl;
  // If linewise, would delete entire line 0 and line 1
  // If characterwise, would delete from [0,0] to [1,1] (exclusive of [1,2])
}


TEST_F(NeovimOracleDebug, DISABLED_InvestigateDAWEdgeCases) {
  // Investigate: when cursor is on a word with leading whitespace (but cursor NOT on whitespace),
  // does daw include leading whitespace from the previous line?

  cerr << "=== Case 1: Word at line start with leading space, cursor on word ===" << endl;
  // " eceba" - cursor on 'e' (col 1), 'c' (col 2), etc.
  {
    Lines source = {"abc", " def"};
    cerr << "Source: 'abc' / ' def'" << endl;

    // daw on 'd' (col 1) - word has leading space, no trailing space
    auto r1 = oracle_->simulate(source, 1, 1, "daw");
    cerr << "  daw at [1,1] ('d'): '" << r1.lines.flatten() << "'" << endl;

    // daw on 'e' (col 2)
    auto r2 = oracle_->simulate(source, 1, 2, "daw");
    cerr << "  daw at [1,2] ('e'): '" << r2.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 2: Word at line start with NO leading space ===" << endl;
  {
    Lines source = {"abc", "def"};
    cerr << "Source: 'abc' / 'def'" << endl;

    auto r1 = oracle_->simulate(source, 1, 0, "daw");
    cerr << "  daw at [1,0] ('d'): '" << r1.lines.flatten() << "'" << endl;

    auto r2 = oracle_->simulate(source, 1, 1, "daw");
    cerr << "  daw at [1,1] ('e'): '" << r2.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 3: Word with trailing space ===" << endl;
  {
    Lines source = {"abc", "def "};
    cerr << "Source: 'abc' / 'def '" << endl;

    auto r1 = oracle_->simulate(source, 1, 0, "daw");
    cerr << "  daw at [1,0] ('d'): '" << r1.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 4: Word with leading AND trailing space ===" << endl;
  {
    Lines source = {"abc", " def "};
    cerr << "Source: 'abc' / ' def '" << endl;

    auto r1 = oracle_->simulate(source, 1, 1, "daw");
    cerr << "  daw at [1,1] ('d'): '" << r1.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 5: Previous line has trailing space ===" << endl;
  {
    Lines source = {"abc ", "def"};
    cerr << "Source: 'abc ' / 'def'" << endl;

    auto r1 = oracle_->simulate(source, 1, 0, "daw");
    cerr << "  daw at [1,0] ('d'): '" << r1.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 6: Previous line has trailing space, current has leading ===" << endl;
  {
    Lines source = {"abc ", " def"};
    cerr << "Source: 'abc ' / ' def'" << endl;

    auto r1 = oracle_->simulate(source, 1, 1, "daw");
    cerr << "  daw at [1,1] ('d'): '" << r1.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 7: Single line tests ===" << endl;
  {
    // Compare single line behavior
    auto r1 = oracle_->simulate({" def"}, 0, 1, "daw");
    cerr << "  daw on ' def' at col 1: '" << r1.lines.flatten() << "'" << endl;

    auto r2 = oracle_->simulate({"def"}, 0, 0, "daw");
    cerr << "  daw on 'def' at col 0: '" << r2.lines.flatten() << "'" << endl;

    auto r3 = oracle_->simulate({"def "}, 0, 0, "daw");
    cerr << "  daw on 'def ' at col 0: '" << r3.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 8: The failing test case ===" << endl;
  {
    Lines source = {" abe. ", " eceba"};
    cerr << "Source: ' abe. ' / ' eceba'" << endl;

    auto r1 = oracle_->simulate(source, 1, 2, "daw");
    cerr << "  daw at [1,2] ('c'): '" << r1.lines.flatten() << "'" << endl;

    // What about at col 1?
    auto r2 = oracle_->simulate(source, 1, 1, "daw");
    cerr << "  daw at [1,1] ('e'): '" << r2.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 9: Verify single-line 'no trailing' behavior ===" << endl;
  {
    // Single line: leading space, no trailing
    auto r1 = oracle_->simulate({"  abc"}, 0, 2, "daw");
    cerr << "  daw on '  abc' at col 2 ('a'): '" << r1.lines.flatten() << "'" << endl;

    // Single line: no leading, no trailing
    auto r2 = oracle_->simulate({"abc"}, 0, 0, "daw");
    cerr << "  daw on 'abc' at col 0: '" << r2.lines.flatten() << "'" << endl;

    // What if there's a word AFTER? (trailing ws leads to next word)
    auto r3 = oracle_->simulate({"abc def"}, 0, 0, "daw");
    cerr << "  daw on 'abc def' at col 0 ('a'): '" << r3.lines.flatten() << "'" << endl;

    auto r4 = oracle_->simulate({"abc def"}, 0, 4, "daw");
    cerr << "  daw on 'abc def' at col 4 ('d'): '" << r4.lines.flatten() << "'" << endl;

    // Word in middle with spaces on both sides
    auto r5 = oracle_->simulate({" abc "}, 0, 1, "daw");
    cerr << "  daw on ' abc ' at col 1 ('a'): '" << r5.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 10: End of line as 'trailing whitespace'? ===" << endl;
  {
    // Does newline count as trailing whitespace?
    Lines source = {"abc", "def"};
    auto r1 = oracle_->simulate(source, 0, 0, "daw");
    cerr << "  daw on 'abc'/'def' at [0,0]: '" << r1.lines.flatten() << "'" << endl;

    // Compare to having actual trailing space
    Lines source2 = {"abc ", "def"};
    auto r2 = oracle_->simulate(source2, 0, 0, "daw");
    cerr << "  daw on 'abc '/'def' at [0,0]: '" << r2.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 11: Does having a word BEFORE matter? ===" << endl;
  {
    // Leading space with no word before
    auto r1 = oracle_->simulate({"  abc"}, 0, 2, "daw");
    cerr << "  '  abc' at col 2: '" << r1.lines.flatten() << "'" << endl;

    // Leading space with word before (on same line)
    auto r2 = oracle_->simulate({"x  abc"}, 0, 3, "daw");
    cerr << "  'x  abc' at col 3 ('a'): '" << r2.lines.flatten() << "'" << endl;

    // Leading space with word before (on previous line)
    auto r3 = oracle_->simulate({"xyz", "  abc"}, 1, 2, "daw");
    cerr << "  'xyz'/'  abc' at [1,2]: '" << r3.lines.flatten() << "'" << endl;

    // No leading space, no trailing space, word before on same line
    auto r4 = oracle_->simulate({"x abc"}, 0, 2, "daw");
    cerr << "  'x abc' at col 2 ('a'): '" << r4.lines.flatten() << "'" << endl;

    // Word at very start of line (col 0), previous line exists
    auto r5 = oracle_->simulate({"xyz", "abc"}, 1, 0, "daw");
    cerr << "  'xyz'/'abc' at [1,0]: '" << r5.lines.flatten() << "'" << endl;
  }

  cerr << endl << "=== Case 12: Trailing space detection ===" << endl;
  {
    // After word: space then another word
    auto r1 = oracle_->simulate({"abc def ghi"}, 0, 4, "daw");
    cerr << "  'abc def ghi' at col 4 ('d'): '" << r1.lines.flatten() << "'" << endl;

    // After word: just space (EOL)
    auto r2 = oracle_->simulate({"abc def "}, 0, 4, "daw");
    cerr << "  'abc def ' at col 4 ('d'): '" << r2.lines.flatten() << "'" << endl;

    // After word: newline (next line exists)
    auto r3 = oracle_->simulate({"abc def", "ghi"}, 0, 4, "daw");
    cerr << "  'abc def'/'ghi' at [0,4] ('d'): '" << r3.lines.flatten() << "'" << endl;
  }
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateParagraph) {
  // Known issue: d{ is LINEWISE in Neovim but our optimizer treats it as characterwise
  // This causes the optimizer to produce incorrect deletion sequences
  // See: https://github.com/... (future issue tracker link)

  Lines source = {"d..,b,b", "eafecf  ", ".f ,b,f"};
  cerr << "Source lines:" << endl;
  for (size_t i = 0; i < source.size(); i++) {
    cerr << "  [" << i << "]: '" << source[i] << "' (len=" << source[i].size() << ")" << endl;
  }

  auto tracer = makeTracer(source, 1, 7);  // pos [1,7]

  // Trace each command.
  tracer.trace("d{");
  tracer.trace("dE");
  tracer.trace("D");

  tracer.printSummary();

  // Also test full sequence
  cerr << endl;
  auto tracer2 = makeTracer(source, 1, 7);
  tracer2.traceFullSequence("d{dED");
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateDAW) {
  // FAIL iter=0 pos=[1,0] seq='dawdd'
  // Source:  abe.
  //  eceba
  // Result:  abe.

  // First, understand the source buffer
  Lines source = {" abe. ", " eceba"};
  cerr << "Source lines:" << endl;
  for (size_t i = 0; i < source.size(); i++) {
    cerr << "  [" << i << "]: '" << source[i] << "' (len=" << source[i].size() << ")" << endl;
  }

  auto tracer = makeTracer(source, 1, 0);  // pos [1,0]

  // Trace each command.
  tracer.trace("daw");
  tracer.trace("dd");

  tracer.printSummary();

  // Also test full sequence
  cerr << endl;
  auto tracer2 = makeTracer(source, 1, 0);
  tracer2.traceFullSequence("dawdd");

  // The optimizer expected this to delete everything
  // Let's see what our VimCore simulation says
  cerr << endl << "=== Our VimCore simulation ===" << endl;
  Lines testLines = source;
  CursorPos pos(1, 0);

  // Simulate daw
  cerr << "Before daw: " << testLines << " pos=(" << pos.line << "," << pos.col << ")" << endl;
  auto range = VimCore::textObject(pos, testLines, false, false);  // daw
  cerr << "daw range: [" << range.begin.line << "," << range.begin.col << "] to ["
       << range.end.line << "," << range.end.col << "]" << endl;

  // Test various daw scenarios to understand Neovim's behavior
  cerr << endl << "=== Additional daw tests ===" << endl;

  // Case 1: daw on space at start of line
  auto r1 = oracle_->simulate({" hello"}, 0, 0, "daw");
  cerr << "daw on ' hello' at col 0: '" << r1.lines.flatten() << "'" << endl;

  // Case 2: daw on word with leading space (same line)
  auto r2 = oracle_->simulate({"abc def"}, 0, 4, "daw");
  cerr << "daw on 'abc def' at col 4 (d): '" << r2.lines.flatten() << "'" << endl;

  // Case 3: daw on space between lines
  auto r3 = oracle_->simulate({"abc ", " def"}, 1, 0, "daw");
  cerr << "daw on 'abc ','  def' at [1,0]: '" << r3.lines.flatten() << "'" << endl;

  // Case 4: daw on only whitespace at start of line
  auto r4 = oracle_->simulate({"abc", "  def"}, 1, 0, "daw");
  cerr << "daw on 'abc','  def' at [1,0]: '" << r4.lines.flatten() << "'" << endl;

  // Case 5: daw on only whitespace at start of line (1 space)
  auto r5 = oracle_->simulate({"abc", " def"}, 1, 0, "daw");
  cerr << "daw on 'abc',' def' at [1,0]: '" << r5.lines.flatten() << "'" << endl;
}

TEST_F(NeovimOracleDebug, DISABLED_InvestigateJCursor) {
  cerr << "=== J cursor placement investigation ===" << endl;

  // Test case from the failing scenario
  Lines lines = {"ccbbfd ", " c"};
  cerr << "Before J: '" << lines[0] << "' / '" << lines[1] << "'" << endl;
  cerr << "Cursor at [0,6]" << endl;

  auto r = oracle_->simulate(lines, 0, 6, "J");
  cerr << endl << "Neovim after J:" << endl;
  cerr << "  Buffer: '" << r.lines.flatten() << "'" << endl;
  cerr << "  Cursor: [" << r.row << "," << r.col << "]" << endl;

  Lines ourLines = lines;
  CursorPos pos(0, 6);
  VimCore::joinLines(ourLines, pos, true);
  cerr << endl << "Our VimCore after J:" << endl;
  cerr << "  Buffer: '" << ourLines.flatten() << "'" << endl;
  cerr << "  Cursor: [" << pos.line << "," << pos.col << "]" << endl;

  if (pos.col != r.col) {
    cerr << endl << "*** CURSOR MISMATCH! Neovim=" << r.col << " Ours=" << pos.col << " ***" << endl;
  }

  cerr << endl << "=== More J tests ===" << endl;

  auto testJ = [&](Lines l, int startCol, const string& label) {
    auto nvim = oracle_->simulate(l, 0, startCol, "J");
    Lines our = l;
    CursorPos p(0, startCol);
    VimCore::joinLines(our, p, true);
    cerr << label << " J at [0," << startCol << "]:" << endl;
    cerr << "  Neovim: '" << nvim.lines.flatten() << "' cursor=[" << nvim.row << "," << nvim.col << "]" << endl;
    cerr << "  Ours:   '" << our.flatten() << "' cursor=[" << p.line << "," << p.col << "]" << endl;
    if (p.col != nvim.col) {
      cerr << "  *** MISMATCH ***" << endl;
    }
  };

  testJ({"abc", "def"}, 2, "'abc'/'def'");
  testJ({"abc ", "def"}, 2, "'abc '/'def'");
  testJ({"abc", " def"}, 2, "'abc'/' def'");
  testJ({"abc ", " def"}, 2, "'abc '/' def'");
  testJ({"abc  ", "def"}, 2, "'abc  '/'def' (2 trailing spaces)");
}


*/
