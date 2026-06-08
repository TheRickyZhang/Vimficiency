export interface CoverageCase {
  name: string;
  passed: boolean;
}

export interface CoverageSuite {
  name: string;
  failures: number;
  cases: CoverageCase[];
}

export interface CoverageBinary {
  totalCases: number;
  failures: number;
  suites: CoverageSuite[];
}

export interface CoverageData {
  generated: number;
  binaries: Record<string, CoverageBinary>;
}
