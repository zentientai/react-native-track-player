#!/usr/bin/env npx tsx

import { execSync } from 'child_process';
import { existsSync, readFileSync } from 'fs';
import { join } from 'path';

type HookInput = {
  tool_input?: {
    file_path?: string;
    filePath?: string;
  };
};

type FailOn = 'exit-code' | 'non-empty-output';

type Check = {
  name: string;
  command: string;
  probability: number;
  agentMessage: string;
  failOn: FailOn;
};

const MAX_OUTPUT_CHARS = 3000;

// Detect which platform a file belongs to
type Platform = 'typescript' | 'android' | 'ios' | 'unknown';

function detectPlatform(filePath: string): Platform {
  if (
    filePath.includes('/android/') ||
    filePath.endsWith('.kt') ||
    filePath.endsWith('.kts') ||
    filePath.endsWith('.java')
  ) {
    return 'android';
  }
  if (
    filePath.includes('/ios/') ||
    filePath.endsWith('.swift') ||
    filePath.endsWith('.m') ||
    filePath.endsWith('.mm') ||
    filePath.endsWith('.h')
  ) {
    return 'ios';
  }
  if (
    filePath.endsWith('.ts') ||
    filePath.endsWith('.tsx') ||
    filePath.endsWith('.js') ||
    filePath.endsWith('.jsx')
  ) {
    return 'typescript';
  }
  return 'unknown';
}

function getChecksForPlatform(platform: Platform, repoRoot: string): Check[] {
  switch (platform) {
    case 'typescript':
      return [
        {
          name: 'lint',
          command: `cd "${repoRoot}" && yarn eslint src web --max-warnings=0 --fix`,
          probability: 0.8,
          failOn: 'exit-code',
          agentMessage:
            'If there are linting errors related to your current work, fix them.',
        },
        {
          name: 'typecheck',
          command: `cd "${repoRoot}" && yarn tsc --noEmit`,
          probability: 0.8,
          failOn: 'exit-code',
          agentMessage:
            'If there are type errors related to your current work, fix them.',
        },
        {
          name: 'test',
          command: `cd "${repoRoot}" && yarn jest --passWithNoTests`,
          probability: 0.5,
          failOn: 'exit-code',
          agentMessage:
            'If there are failing tests related to your current work, fix them.',
        },
        {
          name: 'cpd',
          command: `cd "${repoRoot}" && yarn jscpd src`,
          probability: 0.3,
          failOn: 'non-empty-output',
          agentMessage:
            'If duplicates are related to your current work, consider extracting shared logic.',
        },
        {
          name: 'knip',
          command: `cd "${repoRoot}" && yarn knip`,
          probability: 0.3,
          failOn: 'non-empty-output',
          agentMessage:
            'If unused exports are from your current work, wire them up or remove them.',
        },
      ];
    case 'android':
      return [
        {
          name: 'ktlint',
          command: `cd "${repoRoot}/android" && ./gradlew ktlintCheck --quiet`,
          probability: 0.8,
          failOn: 'exit-code',
          agentMessage:
            'If there are ktlint errors related to your current work, fix them. You can auto-fix with: cd android && ./gradlew ktlintFormat',
        },
        {
          name: 'detekt',
          command: `cd "${repoRoot}/android" && ./gradlew detekt --quiet`,
          probability: 0.8,
          failOn: 'exit-code',
          agentMessage:
            'If there are detekt issues related to your current work, fix them.',
        },
      ];
    case 'ios':
      return [
        {
          name: 'swiftlint',
          command: `cd "${repoRoot}/ios" && swiftlint lint --quiet`,
          probability: 0.8,
          failOn: 'exit-code',
          agentMessage:
            'If there are SwiftLint errors related to your current work, fix them.',
        },
      ];
    default:
      return [];
  }
}

async function readStdin(): Promise<string> {
  return new Promise((resolve) => {
    let data = '';
    process.stdin.setEncoding('utf8');
    process.stdin.on('data', (chunk) => {
      data += chunk;
    });
    process.stdin.on('end', () => {
      resolve(data);
    });
  });
}

function extractFilePath(input: HookInput): string | undefined {
  return input.tool_input?.file_path ?? input.tool_input?.filePath;
}

function findRepoRoot(filePath: string): string | undefined {
  // Walk up from the file to find the repo root (has package.json with "react-native-track-player")
  let dir = filePath;
  for (let i = 0; i < 20; i++) {
    dir = dir.substring(0, dir.lastIndexOf('/'));
    if (!dir) break;
    const pkgPath = join(dir, 'package.json');
    if (existsSync(pkgPath)) {
      try {
        const pkg = JSON.parse(readFileSync(pkgPath, 'utf-8'));
        if (pkg.name === 'react-native-track-player') {
          return dir;
        }
      } catch {
        continue;
      }
    }
  }
  return undefined;
}

function shouldRunCheck(probability: number): boolean {
  return Math.random() < probability;
}

type CheckResult = {
  name: string;
  agentMessage: string;
  output: string;
  failed: boolean;
};

function truncateOutput(output: string): string {
  if (output.length <= MAX_OUTPUT_CHARS) {
    return output;
  }
  const truncated = output.slice(0, MAX_OUTPUT_CHARS);
  const lastNewline = truncated.lastIndexOf('\n');
  const cleanCut =
    lastNewline > 0 ? truncated.slice(0, lastNewline) : truncated;
  return (
    cleanCut +
    `\n\n... (truncated, ${output.length - cleanCut.length} chars omitted)`
  );
}

function stripNpmBoilerplate(raw: string): string {
  return raw
    .split('\n')
    .filter(
      (line) =>
        !line.startsWith('> ') && !line.startsWith('npm ') && line.trim() !== ''
    )
    .join('\n')
    .trim();
}

function runCheck(check: Check): CheckResult {
  let exitedClean = true;
  let stdout = '';
  let stderr = '';

  try {
    const buf = execSync(check.command, {
      stdio: ['pipe', 'pipe', 'pipe'],
      timeout: 120000,
    });
    stdout = buf.toString();
  } catch (error) {
    exitedClean = false;
    const execError = error as { stderr?: Buffer; stdout?: Buffer };
    stdout = execError.stdout?.toString() ?? '';
    stderr = execError.stderr?.toString() ?? '';
  }

  let output = [stdout, stderr].filter(Boolean).join('\n');
  const strippedOutput = stripNpmBoilerplate(output);

  const failed =
    check.failOn === 'exit-code' ? !exitedClean : strippedOutput.length > 0;

  if (!failed) {
    return {
      name: check.name,
      agentMessage: check.agentMessage,
      output: '',
      failed: false,
    };
  }

  output = truncateOutput(output);

  return {
    name: check.name,
    agentMessage: check.agentMessage,
    output,
    failed: true,
  };
}

async function main(): Promise<void> {
  const input = await readStdin();

  let parsed: HookInput;
  try {
    parsed = JSON.parse(input) as HookInput;
  } catch {
    process.exit(0);
  }

  const filePath = extractFilePath(parsed);
  if (!filePath) {
    process.exit(0);
  }

  const repoRoot = findRepoRoot(filePath);
  if (!repoRoot) {
    process.exit(0);
  }

  const platform = detectPlatform(filePath);
  if (platform === 'unknown') {
    process.exit(0);
  }

  const checks = getChecksForPlatform(platform, repoRoot);
  const results: CheckResult[] = [];

  for (const check of checks) {
    if (!shouldRunCheck(check.probability)) {
      continue;
    }
    results.push(runCheck(check));
  }

  const failures = results.filter((r) => r.failed);

  if (failures.length === 0) {
    process.exit(0);
  }

  for (const result of failures) {
    process.stderr.write(`\n=== ${result.name.toUpperCase()} ===\n`);
    process.stderr.write(`[Agent Instructions] ${result.agentMessage}\n\n`);
    process.stderr.write(result.output);
    process.stderr.write('\n');
  }

  process.exit(2);
}

main();
