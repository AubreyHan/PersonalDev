import { describe, it, expect } from 'vitest';
import { hashPassword, verifyPassword, signJwt, verifyJwt } from '../api/services/crypto';

describe('functions/api/services/crypto', () => {
  it('should hash and verify passwords correctly', async () => {
    const rawPass = 'P@ss=w0rd';
    const hash = await hashPassword(rawPass);
    expect(hash).toContain(':');

    const isValid = await verifyPassword(rawPass, hash);
    expect(isValid).toBe(true);

    const isInvalid = await verifyPassword('wrongpassword', hash);
    expect(isInvalid).toBe(false);
  });

  it('should sign and verify JWT tokens', async () => {
    const payload = { userId: '123', role: 'admin' };
    const token = await signJwt(payload, 'test-secret');
    expect(typeof token).toBe('string');

    const decoded = await verifyJwt<{ userId: string; role: string }>(token, 'test-secret');
    expect(decoded?.userId).toBe('123');
    expect(decoded?.role).toBe('admin');

    const invalid = await verifyJwt(token, 'different-secret');
    expect(invalid).toBeNull();
  });
});
