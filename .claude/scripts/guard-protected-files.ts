#!/usr/bin/env npx tsx

import { execSync } from 'child_process';
import { unlinkSync } from 'fs';

type HookInput = {
  tool_input?: {
    file_path?: string;
    filePath?: string;
  };
};

function readStdin(): Promise<string> {
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

function main(): void {
  if (process.env.CLAUDE_CODE_GUARD !== '1') {
    process.exit(0);
  }

  readStdin()
    .then((raw) => {
      let parsed: HookInput;
      try {
        parsed = JSON.parse(raw) as HookInput;
      } catch {
        process.exit(0);
        return;
      }

      const filePath = extractFilePath(parsed);
      if (!filePath) {
        process.exit(0);
        return;
      }

      const basename = filePath.split('/').pop() ?? '';
      const isEslintConfig = basename.toLowerCase().includes('eslint');
      const isLefthookConfig = basename === 'lefthook.yml';
      const isJscpdConfig = basename.endsWith('.jscpd.json');
      const isSwiftlintConfig = basename === '.swiftlint.yml';
      const isDetektConfig = basename === 'detekt.yml';

      const guard = isEslintConfig
        ? { label: 'ESLINT CONFIG', file: basename }
        : isLefthookConfig
          ? { label: 'LEFTHOOK CONFIG', file: 'lefthook.yml' }
          : isJscpdConfig
            ? { label: 'JSCPD CONFIG', file: basename }
            : isSwiftlintConfig
              ? { label: 'SWIFTLINT CONFIG', file: '.swiftlint.yml' }
              : isDetektConfig
                ? { label: 'DETEKT CONFIG', file: 'detekt.yml' }
                : null;

      if (!guard) {
        process.exit(0);
        return;
      }

      try {
        execSync(`git checkout -- "${filePath}"`, { stdio: 'pipe' });
      } catch {
        try {
          unlinkSync(filePath);
        } catch {
          // Best effort
        }
      }

      process.stderr.write(`\n=== GUARDRAIL: ${guard.label} PROTECTED ===\n`);
      process.stderr.write(
        `[Agent Instructions] You modified a protected file (${guard.file}). This change has been reverted. This file is a protected guardrail and must not be modified by Claude Code.\n`
      );

      process.exit(2);
    })
    .catch(() => {
      process.exit(0);
    });
}

main();
