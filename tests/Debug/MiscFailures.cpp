/*
TEST_F(NeovimOracleDebug, DISABLED_InvestigateRemainingFailures) {
  // These failures have been fixed:
  // - d( sentence motion when cursor on trailing whitespace at EOL
  // - daw text object when trailing whitespace ends at EOL
  cerr << "=== Failure 1: dddawD at [0,2] ===" << endl;
  {
    Lines source = {"ede.", "ceddc ", " dfdcad"};
    cerr << "Source: '" << source[0] << "' / '" << source[1] << "' / '" << source[2] << "'" << endl;
    cerr << "Line lengths: " << source[0].size() << ", " << source[1].size() << ", " << source[2].size() << endl;

    auto tracer = makeTracer(source, 0, 2);
    tracer.trace("dd");
    tracer.trace("daw");
    tracer.trace("D");
    tracer.printSummary();
    cerr << "Expected: empty" << endl;

    // Now verify what our VimCore thinks
    cerr << endl << "=== Our VimCore simulation ===" << endl;
    Lines testLines = source;
    Position pos(0, 2);

    // Simulate dd
    cerr << "Before dd: " << testLines << " pos=(" << pos.line << "," << pos.col << ")" << endl;
    VimCore::deleteRangeLinewise(testLines, LineRange(0, 0), pos);
    cerr << "After dd: " << testLines << " pos=(" << pos.line << "," << pos.col << ")" << endl;

    // Simulate daw - first check what textObjectRange returns
    cerr << endl << "textObjectRange for daw at (" << pos.line << "," << pos.col << "):" << endl;
    Range dawRange = VimCore::textObjectRange(pos, testLines, false, false, 0, 0, false, false);
    cerr << "  Range: [" << dawRange.first.line << "," << dawRange.first.col << "] to ["
         << dawRange.last.line << "," << dawRange.last.col << "]" << endl;

    // Apply the deletion
    VimCore::deleteRange(testLines, dawRange, pos, Mode::Normal);
    cerr << "After daw: " << testLines << " pos=(" << pos.line << "," << pos.col << ")" << endl;
    cerr << "  Line count: " << testLines.size() << ", Line 0 empty: " << testLines[0].empty() << endl;
  }

  cerr << endl << "=== Failure 2: dawddD at [1,0] ===" << endl;
  {
    Lines source = {"ede.", "ceddc ", " dfdcad"};
    cerr << "Source: '" << source[0] << "' / '" << source[1] << "' / '" << source[2] << "'" << endl;

    auto tracer = makeTracer(source, 1, 0);
    tracer.trace("daw");
    tracer.trace("dd");
    tracer.trace("D");
    tracer.printSummary();
    cerr << "Expected: empty" << endl;
  }

  cerr << endl << "=== Failure 2b: dawddD at [1,2] ===" << endl;
  {
    Lines source = {"ede.", "ceddc ", " dfdcad"};
    cerr << "Source: '" << source[0] << "' / '" << source[1] << "' / '" << source[2] << "'" << endl;

    auto tracer = makeTracer(source, 1, 2);
    tracer.trace("daw");
    tracer.trace("dd");
    tracer.trace("D");
    tracer.printSummary();
    cerr << "Expected: empty" << endl;
  }

  cerr << endl << "=== Understanding daw on single word line with trailing space ===" << endl;
  {
    Lines source = {"abc", "ceddc ", "def"};
    cerr << "Source: 'abc' / 'ceddc ' / 'def'" << endl;

    // daw at start of "ceddc " - should delete the word and trailing space
    auto r1 = oracle_->simulate(source, 1, 0, "daw");
    cerr << "  daw at [1,0]: '" << r1.lines.flatten() << "' cursor=[" << r1.row << "," << r1.col << "]" << endl;

    // What about when it's the only content on line?
    Lines source2 = {"abc", "word ", "def"};
    auto r2 = oracle_->simulate(source2, 1, 0, "daw");
    cerr << "  daw on 'abc'/'word '/'def' at [1,0]: '" << r2.lines.flatten() << "' cursor=[" << r2.row << "," << r2.col << "]" << endl;

    // Does daw join lines when deleting entire line content?
    Lines source3 = {"abc", "word", "def"};
    auto r3 = oracle_->simulate(source3, 1, 0, "daw");
    cerr << "  daw on 'abc'/'word'/'def' at [1,0]: '" << r3.lines.flatten() << "' cursor=[" << r3.row << "," << r3.col << "]" << endl;
  }

  cerr << endl << "=== dw behavior investigation ===" << endl;
  {
    // What does dw do at end of line?
    Lines source = {"b,cf", " f.ef,", "c .ecee"};
    auto r1 = oracle_->simulate(source, 0, 2, "dw");
    cerr << "  'b,cf'/... dw at [0,2] ('c'): '" << r1.lines.flatten() << "' cursor=[" << r1.row << "," << r1.col << "]" << endl;

    // dw at very end of line
    auto r2 = oracle_->simulate(source, 0, 3, "dw");
    cerr << "  'b,cf'/... dw at [0,3] ('f'): '" << r2.lines.flatten() << "' cursor=[" << r2.row << "," << r2.col << "]" << endl;

    // Compare: does dw cross lines?
    Lines source2 = {"abc", "def"};
    auto r3 = oracle_->simulate(source2, 0, 2, "dw");
    cerr << "  'abc'/'def' dw at [0,2] ('c'): '" << r3.lines.flatten() << "' cursor=[" << r3.row << "," << r3.col << "]" << endl;

    // What about when there's whitespace at next line start?
    Lines source3 = {"abc", " def"};
    auto r4 = oracle_->simulate(source3, 0, 2, "dw");
    cerr << "  'abc'/' def' dw at [0,2] ('c'): '" << r4.lines.flatten() << "' cursor=[" << r4.row << "," << r4.col << "]" << endl;

    // Key test: trailing space on same line vs newline
    Lines source4 = {"abc ", "def"};  // trailing space
    auto r5 = oracle_->simulate(source4, 0, 2, "dw");
    cerr << "  'abc '/'def' dw at [0,2] ('c'): '" << r5.lines.flatten() << "' cursor=[" << r5.row << "," << r5.col << "]" << endl;

    Lines source5 = {"abc  ", "def"};  // two trailing spaces
    auto r6 = oracle_->simulate(source5, 0, 2, "dw");
    cerr << "  'abc  '/'def' dw at [0,2] ('c'): '" << r6.lines.flatten() << "' cursor=[" << r6.row << "," << r6.col << "]" << endl;

    // dw in middle of line with word after
    Lines source6 = {"abc def"};
    auto r7 = oracle_->simulate(source6, 0, 1, "dw");
    cerr << "  'abc def' dw at [0,1] ('b'): '" << r7.lines.flatten() << "' cursor=[" << r7.row << "," << r7.col << "]" << endl;

    auto r8 = oracle_->simulate(source6, 0, 2, "dw");
    cerr << "  'abc def' dw at [0,2] ('c'): '" << r8.lines.flatten() << "' cursor=[" << r8.row << "," << r8.col << "]" << endl;
  }

  cerr << endl << "=== Sentence motion basics ===" << endl;
  {
    // Test what constitutes a sentence boundary
    Lines source = {"Hello. World", "Next line."};
    auto r1 = oracle_->simulate(source, 0, 7, "d(");  // cursor on 'W'
    cerr << "  'Hello. World' d( at col 7 ('W'): '" << r1.lines.flatten() << "'" << endl;

    auto r2 = oracle_->simulate(source, 0, 0, "d)");  // cursor on 'H'
    cerr << "  'Hello. World' d) at col 0 ('H'): '" << r2.lines.flatten() << "'" << endl;

    // No punctuation - is newline a sentence boundary?
    Lines source2 = {"abc", "def"};
    auto r3 = oracle_->simulate(source2, 1, 0, "d(");
    cerr << "  'abc'/'def' d( at [1,0]: '" << r3.lines.flatten() << "'" << endl;
  }
}
*/
