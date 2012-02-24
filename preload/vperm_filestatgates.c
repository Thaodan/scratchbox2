/*
 * libsb2 -- GATE functions for file status simulation (stat(),chown() etc)
 *
 * Copyright (C) 2011 Nokia Corporation.
 * Author: Lauri T. Aarnio
*/

/*
    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
*/

#include "sb2.h"
#include "sb2_stat.h"
#include "sb2_vperm.h"
#include "libsb2.h"
#include "exported.h"
#include "rule_tree.h"
#include "rule_tree_rpc.h"

static int get_stat_for_fxxat(
	const char *realfnname,
	int dirfd,
	const mapping_results_t *mapped_filename,
	int flags,
	struct stat *statbuf)
{
	int	r;

	r = real_fstatat(dirfd, mapped_filename->mres_result_path, statbuf, flags);
	if (r < 0) {
		int e = errno;
		SB_LOG(SB_LOGLEVEL_DEBUG, "%s: returns %d, errno=%d",
			realfnname, r, e);
		errno = e;
	}
	return(r);
}

static void vperm_clear_all_if_virtualized(
        const char *realfnname,
	struct stat *statbuf)
{
	ruletree_inodestat_handle_t	handle;
	inodesimu_t			istat_struct;

	ruletree_init_inodestat_handle(&handle, statbuf->st_dev, statbuf->st_ino);
	if (ruletree_find_inodestat(&handle, &istat_struct) == 0) {
		/* vperms exist for this inode */
		if (istat_struct.inodesimu_active_fields != 0) {
			SB_LOG(SB_LOGLEVEL_DEBUG, "%s: clear dev=%llu ino=%llu", 
				realfnname, (unsigned long long)statbuf->st_dev,
				(unsigned long long)statbuf->st_ino);
			ruletree_rpc__vperm_clear(statbuf->st_dev, statbuf->st_ino);
		}
	}
}

/* ======================= stat() variants ======================= */

int __xstat_gate(int *result_errno_ptr,
	int (*real___xstat_ptr)(int ver, const char *filename, struct stat *buf),
        const char *realfnname,
	int ver,
	const mapping_results_t *mapped_filename,
	struct stat *buf)
{
	int	r;
	
	SB_LOG(SB_LOGLEVEL_DEBUG, "%s gate: calling sb2_stat(%s)",
		realfnname, mapped_filename->mres_result_path);
	r = sb2_stat_file(mapped_filename->mres_result_path, buf, result_errno_ptr,
		real___xstat_ptr, ver, NULL);
	SB_LOG(SB_LOGLEVEL_DEBUG, "%s gate: sb2_stat(%s) returned",
		realfnname, mapped_filename->mres_result_path);
	return(r);
}

int __xstat64_gate(int *result_errno_ptr,
	int (*real___xstat64_ptr)(int ver, const char *filename, struct stat64 *buf),
        const char *realfnname,
	int ver,
	const mapping_results_t *mapped_filename,
	struct stat64 *buf)
{
	int	r;
	
	SB_LOG(SB_LOGLEVEL_DEBUG, "%s gate: calling sb2_stat(%s)",
		realfnname, mapped_filename->mres_result_path);
	r = sb2_stat64_file(mapped_filename->mres_result_path, buf, result_errno_ptr,
		real___xstat64_ptr, ver, NULL);
	SB_LOG(SB_LOGLEVEL_DEBUG, "%s gate: sb2_stat(%s) returned",
		realfnname, mapped_filename->mres_result_path);
	return(r);
}

/* int __lxstat(int ver, const char *filename, struct stat *buf) */
int __lxstat_gate(int *result_errno_ptr,
	int (*real___lxstat_ptr)(int ver, const char *filename, struct stat *buf),
        const char *realfnname,
	int ver,
	const mapping_results_t *mapped_filename,
	struct stat *buf)
{
	int	r;
	
	SB_LOG(SB_LOGLEVEL_DEBUG, "%s gate: calling sb2_stat(%s)",
		realfnname, mapped_filename->mres_result_path);
	r = sb2_stat_file(mapped_filename->mres_result_path, buf, result_errno_ptr,
		real___lxstat_ptr, ver, NULL);
	SB_LOG(SB_LOGLEVEL_DEBUG, "%s gate: sb2_stat(%s) returned",
		realfnname, mapped_filename->mres_result_path);
	return(r);
}

/* int __lxstat64(int ver, const char *filename, struct stat64 *buf) */
int __lxstat64_gate(int *result_errno_ptr,
	int (*real___lxstat64_ptr)(int ver, const char *filename, struct stat64 *buf),
        const char *realfnname,
	int ver,
	const mapping_results_t *mapped_filename,
	struct stat64 *buf)
{
	int	r;
	
	SB_LOG(SB_LOGLEVEL_DEBUG, "%s gate: calling sb2_stat(%s)",
		realfnname, mapped_filename->mres_result_path);
	r = sb2_stat64_file(mapped_filename->mres_result_path, buf, result_errno_ptr,
		real___lxstat64_ptr, ver, NULL);
	SB_LOG(SB_LOGLEVEL_DEBUG, "%s gate: sb2_stat(%s) returned",
		realfnname, mapped_filename->mres_result_path);
	return(r);
}

int fstat_gate(int *result_errno_ptr,
	int (*real_fstat_ptr)(int fd, struct stat *buf),
        const char *realfnname,
	int fd,
	struct stat *buf)
{
	int res;

	res = (*real_fstat_ptr)(fd, buf);
	if (res == 0) {
		i_virtualize_struct_stat(buf, NULL);
	} else {
		*result_errno_ptr = errno;
	}
	return(res);
}

int __fxstat_gate(int *result_errno_ptr,
	int (*real___fxstat_ptr)(int ver, int fd, struct stat *buf),
        const char *realfnname,
	int ver,
	int fd,
	struct stat *buf)
{
	int res;

	res = (*real___fxstat_ptr)(ver, fd, buf);
	if (res == 0) {
		i_virtualize_struct_stat(buf, NULL);
	} else {
		*result_errno_ptr = errno;
	}
	return(res);
}

int __fxstat64_gate(int *result_errno_ptr,
	int (*real___fxstat64_ptr)(int ver, int fd, struct stat64 *buf),
        const char *realfnname,
	int ver,
	int fd,
	struct stat64 *buf64)
{
	int res;

	res = (*real___fxstat64_ptr)(ver, fd, buf64);
	if (res == 0) {
		i_virtualize_struct_stat(NULL, buf64);
	} else {
		*result_errno_ptr = errno;
	}
	return(res);
}

int __fxstatat_gate(int *result_errno_ptr,
	int (*real___fxstatat_ptr)(int ver, int dirfd, const char *pathname, struct stat *buf, int flags),
	const char *realfnname,
	int ver,
	int dirfd,
	const mapping_results_t *mapped_filename,
	struct stat *buf,
	int flags)
{
	int	res;

	res = (*real___fxstatat_ptr)(ver, dirfd, mapped_filename->mres_result_path, buf, flags);
	*result_errno_ptr = errno;
	if (res == 0) {
		i_virtualize_struct_stat(buf, NULL);
	} else {
		*result_errno_ptr = errno;
	}
	return(res);
}

int __fxstatat64_gate(int *result_errno_ptr,
	int (*real___fxstatat64_ptr)(int ver, int dirfd, const char *pathname, struct stat64 *buf64, int flags),
	const char *realfnname,
	int ver,
	int dirfd,
	const mapping_results_t *mapped_filename,
	struct stat64 *buf64,
	int flags)
{
	int	res;

	res = (*real___fxstatat64_ptr)(ver, dirfd, mapped_filename->mres_result_path, buf64, flags);
	if (res == 0) {
		i_virtualize_struct_stat(NULL, buf64);
	} else {
		*result_errno_ptr = errno;
	}
	return(res);
}

/* ======================= chown() variants ======================= */

static void vperm_chown(
        const char *realfnname,
	struct stat *statbuf,
	uid_t owner,
	gid_t group)
{
	int set_uid = 0;
	int set_gid = 0;
	int release_uid = 0;
	int release_gid = 0;

	SB_LOG(SB_LOGLEVEL_DEBUG, "%s: real fn=>EPERM, virtualize "
		" (uid=%d, gid=%d)",
		realfnname, statbuf->st_uid, statbuf->st_gid);
	if (owner != (uid_t)-1) {
		if (statbuf->st_uid != owner) set_uid = 1;
		else release_uid = 1;
	}
	if (group != (gid_t)-1) {
		if (statbuf->st_gid != group) set_gid = 1;
		else release_gid = 1;
	}

	if (set_uid || set_gid) {
		ruletree_rpc__vperm_set_ids((uint64_t)statbuf->st_dev,
			(uint64_t)statbuf->st_ino, set_uid, owner, set_gid, group);
	}
	if (release_uid || release_gid) {
		ruletree_rpc__vperm_release_ids((uint64_t)statbuf->st_dev,
			(uint64_t)statbuf->st_ino, release_uid, release_gid);
	}
	
	/* Assume success. FIXME; check the result. */
}

int fchownat_gate(int *result_errno_ptr,
	int (*real_fchownat_ptr)(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags),
        const char *realfnname,
	int dirfd,
	const mapping_results_t *mapped_filename,
	uid_t owner,
	gid_t group,
	int flags)
{
	int	res;
	struct stat statbuf;

	/* first try the real function */
	res = (*real_fchownat_ptr)(dirfd, mapped_filename->mres_result_path, owner, group, flags);
	if (res == 0) {
		/* OK, success. */
		/* If this inode has been virtualized, update DB */
		if (get_vperm_num_active_inodestats() > 0) {
			/* there are active vperm inodestat nodes */
			if (get_stat_for_fxxat(realfnname, dirfd, mapped_filename, flags, &statbuf) == 0) {
				/* since the real function succeeds, vperm_chown() will now
				 * release simulated UID and/or GID */
				vperm_chown(realfnname, &statbuf, owner, group);
			}
		}
	} else {
		int	e = errno;

		if (e == EPERM) {
			if (get_stat_for_fxxat(realfnname, dirfd, mapped_filename, flags, &statbuf) == 0) {
				vperm_chown(realfnname, &statbuf, owner, group);
				res = 0;
			} else {
				/* This should never happen */
				*result_errno_ptr = EPERM;
				res = -1;
			}
		} else {
			*result_errno_ptr = errno;
		}
	}
	SB_LOG(SB_LOGLEVEL_DEBUG, "fchownat: returns %d", res);
	return(res);
}

int chown_gate(int *result_errno_ptr,
	int (*real_chown_ptr)(const char *pathname, uid_t owner, gid_t group),
        const char *realfnname,
	const mapping_results_t *mapped_filename,
	uid_t owner,
	gid_t group)
{
	int	res;
	struct stat statbuf;

	/* first try the real function */
	res = (*real_chown_ptr)(mapped_filename->mres_result_path, owner, group);
	if (res == 0) {
		/* OK, success. */
		/* If this inode has been virtualized, update DB */
		if (get_vperm_num_active_inodestats() > 0) {
			/* there are active vperm inodestat nodes */
			if (real_stat(mapped_filename->mres_result_path, &statbuf) == 0) {
				/* since the real function succeeds, vperm_chown() will now
				 * release simulated UID and/or GID */
				vperm_chown(realfnname, &statbuf, owner, group);
			}
		}
	} else {
		int	e = errno;

		if (e == EPERM) {
			res = real_stat(mapped_filename->mres_result_path, &statbuf);
			if (res == 0) vperm_chown(realfnname, &statbuf, owner, group);
			else {
				*result_errno_ptr = EPERM;
				res = -1;
			}
		} else {
			*result_errno_ptr = errno;
		}
	}
	SB_LOG(SB_LOGLEVEL_DEBUG, "chown: returns %d", res);
	return(res);
}

int lchown_gate(int *result_errno_ptr,
	int (*real_lchown_ptr)(const char *pathname, uid_t owner, gid_t group),
        const char *realfnname,
	const mapping_results_t *mapped_filename,
	uid_t owner,
	gid_t group)
{
	int	res;
	struct stat statbuf;

	/* first try the real function */
	res = (*real_lchown_ptr)(mapped_filename->mres_result_path, owner, group);
	if (res == 0) {
		/* OK, success. */
		/* If this inode has been virtualized, update DB */
		if (get_vperm_num_active_inodestats() > 0) {
			/* there are active vperm inodestat nodes */
			if (real_lstat(mapped_filename->mres_result_path, &statbuf) == 0) {
				/* since the real function succeeds, vperm_chown() will now
				 * release simulated UID and/or GID */
				vperm_chown(realfnname, &statbuf, owner, group);
			}
		}
	} else {
		int	e = errno;

		if (e == EPERM) {
			res = real_lstat(mapped_filename->mres_result_path, &statbuf);
			if (res == 0) vperm_chown(realfnname, &statbuf, owner, group);
			else {
				res = -1;
				*result_errno_ptr = EPERM;
			}
		} else {
			*result_errno_ptr = errno;
		}
	}
	SB_LOG(SB_LOGLEVEL_DEBUG, "lchown: returns %d", res);
	return(res);
}

int fchown_gate(int *result_errno_ptr,
	int (*real_fchown_ptr)(int fd, uid_t owner, gid_t group),
        const char *realfnname,
	int fd,
	uid_t owner,
	gid_t group)
{
	int	res;
	struct stat statbuf;

	/* first try the real function */
	res = (*real_fchown_ptr)(fd, owner, group);
	if (res == 0) {
		/* OK, success. */
		/* If this inode has been virtualized, update DB */
		if (get_vperm_num_active_inodestats() > 0) {
			/* there are active vperm inodestat nodes */
			if (real_fstat(fd, &statbuf) == 0) {
				/* since the real function succeeds, vperm_chown() will now
				 * release simulated UID and/or GID */
				vperm_chown(realfnname, &statbuf, owner, group);
			}
		}
	} else {
		int	e = errno;

		if (e == EPERM) {
			res = real_fstat(fd, &statbuf);
			if (res == 0) vperm_chown(realfnname, &statbuf, owner, group);
			else {
				res = -1;
				*result_errno_ptr = EPERM;
			}
		} else {
			*result_errno_ptr = errno;
		}
	}
	SB_LOG(SB_LOGLEVEL_DEBUG, "fchown: returns %d", res);
	return(res);
}

/* ======================= chmod() variants ======================= */

/* set/release st_mode virtualization */
static void vperm_chmod(
        const char *realfnname,
	struct stat *statbuf,
	mode_t virt_mode,
	mode_t suid_sgid_bits)
{
	SB_LOG(SB_LOGLEVEL_DEBUG, "%s: virtualize stat "
		" (real mode=0%o, new virtual mode=0%o, suid/sgid=0%o)",
		realfnname, statbuf->st_mode, virt_mode, suid_sgid_bits);
	if (((statbuf->st_mode & ~(S_IFMT | S_ISUID | S_ISGID)) !=
		    (virt_mode & ~(S_IFMT | S_ISUID | S_ISGID))) ||
	    ((statbuf->st_mode & (S_ISUID | S_ISGID)) != suid_sgid_bits))
		ruletree_rpc__vperm_set_mode((uint64_t)statbuf->st_dev,
			(uint64_t)statbuf->st_ino, statbuf->st_mode,
			virt_mode & ~(S_IFMT | S_ISUID | S_ISGID),
			suid_sgid_bits & (S_ISUID | S_ISGID));
	else
		ruletree_rpc__vperm_release_mode((uint64_t)statbuf->st_dev,
			(uint64_t)statbuf->st_ino);
	/* Assume success. FIXME; check the result. */
}

static int vperm_chmod_if_simulated_device(
        const char *realfnname,
	struct stat *statbuf,
	mode_t mode,
	mode_t suid_sgid_bits)
{
	ruletree_inodestat_handle_t	handle;
	inodesimu_t			istat_struct;

	ruletree_init_inodestat_handle(&handle, statbuf->st_dev, statbuf->st_ino);
	if (ruletree_find_inodestat(&handle, &istat_struct) == 0) {
		/* vperms exist for this inode */
		if (istat_struct.inodesimu_active_fields & RULETREE_INODESTAT_SIM_DEVNODE) {
			/* A simulated device; never set real mode for this,
			 * real mode is 0000 intentionally.  */
			SB_LOG(SB_LOGLEVEL_DEBUG, "%s: set mode of simulated device", __func__);
			vperm_chmod(realfnname, statbuf, mode, suid_sgid_bits);
			return(0);
		}
	}
	return(-1);
}

int fchmodat_gate(int *result_errno_ptr,
	int (*real_fchmodat_ptr)(int dirfd, const char *pathname, mode_t mode, int flags),
        const char *realfnname,
	int dirfd,
	const mapping_results_t *mapped_filename,
	mode_t mode,
	int flags)
{
	int		res;
	struct stat	statbuf;
	mode_t		suid_sgid_bits;
	int		e;

	/* separate SUID and SGID bits from mode. Never set
	 * real SUID/SGID bits. */
	suid_sgid_bits = mode & (S_ISUID | S_ISGID);
	mode &= ~(S_ISUID | S_ISGID);

	/* Use stat only if there are active vperm inodestat nodes */
	if (get_vperm_num_active_inodestats() > 0) {
		/* check if this file already has vperms */
		if (get_stat_for_fxxat(realfnname, dirfd, mapped_filename, 0, &statbuf) == 0) {
			/* Exists & got stat */
			if (vperm_chmod_if_simulated_device(realfnname, &statbuf, mode,
					suid_sgid_bits) == 0)
				return(0);
			/* else not a simulated device, continue here */
		} /* else the real function sets errno */
	}

	/* try the real function */
	res = (*real_fchmodat_ptr)(dirfd, mapped_filename->mres_result_path, mode, flags);
	e = errno;

	/* need to update vperm state if
	 *  - SUID/SGID is used
	 *  - real function failed due to permissions
	 *  - real function succeeded and there are active vperms.
	 * Don't need to update vperms, if
	 *  - nothing active in vperm tree
	 *  - other errors than EPERM.
	*/
	if (suid_sgid_bits ||
	    ((res < 0) && ( e == EPERM)) ||
	    ((res == 0) && (get_vperm_num_active_inodestats() > 0))) {
		if (get_stat_for_fxxat(realfnname, dirfd, mapped_filename, flags, &statbuf) == 0) {
			vperm_chmod(realfnname, &statbuf, mode, suid_sgid_bits);
			res = 0;
		} else { /* real fn didn't work, and can't stat */
			*result_errno_ptr = e;
			res = -1;
		}
	}

	SB_LOG(SB_LOGLEVEL_DEBUG, "fchmodat: returns %d", res);
	return(res);
}

int fchmod_gate(int *result_errno_ptr,
	int (*real_fchmod_ptr)(int fildes, mode_t mode),
        const char *realfnname,
	int fd,
	mode_t mode)
{
	int		res;
	struct stat	statbuf;
	mode_t		suid_sgid_bits;
	int		e;

	/* separate SUID and SGID bits from mode. Never set
	 * real SUID/SGID bits. */
	suid_sgid_bits = mode & (S_ISUID | S_ISGID);
	mode &= ~(S_ISUID | S_ISGID);

	/* Use stat only if there are active vperm inodestat nodes */
	if (get_vperm_num_active_inodestats() > 0) {
		if (real_fstat(fd, &statbuf) == 0) {
			if (vperm_chmod_if_simulated_device(realfnname, &statbuf, mode,
					suid_sgid_bits) == 0)
				return(0);
			/* else not a simulated device, continue here */
		} /* else the real function sets errno */
	}

	res = (*real_fchmod_ptr)(fd, mode);
	e = errno;

	/* need to update vperm state if
	 *  - SUID/SGID is used
	 *  - real function failed due to permissions
	 *  - real function succeeded and there are active vperms.
	 * Don't need to update vperms, if
	 *  - nothing active in vperm tree
	 *  - other errors than EPERM.
	*/
	if (suid_sgid_bits ||
	    ((res < 0) && ( e == EPERM)) ||
	    ((res == 0) && (get_vperm_num_active_inodestats() > 0))) {
		if (real_fstat(fd, &statbuf) == 0) {
			vperm_chmod(realfnname, &statbuf, mode, suid_sgid_bits);
			res = 0;
		} else { /* real fn didn't work, and can't stat */
			*result_errno_ptr = e;
			res = -1;
		}
	}

	SB_LOG(SB_LOGLEVEL_DEBUG, "%s: returns %d", __func__, res);
	return(res);
}

int chmod_gate(int *result_errno_ptr,
	int (*real_chmod_ptr)(const char *path, mode_t mode),
        const char *realfnname,
	const mapping_results_t *mapped_filename,
	mode_t mode)
{
	int		res;
	struct stat	statbuf;
	mode_t		suid_sgid_bits;
	int		e;

	/* separate SUID and SGID bits from mode. Never set
	 * real SUID/SGID bits. */
	suid_sgid_bits = mode & (S_ISUID | S_ISGID);
	mode &= ~(S_ISUID | S_ISGID);

	/* Use stat only if there are active vperm inodestat nodes */
	if (get_vperm_num_active_inodestats() > 0) {
		if (real_stat(mapped_filename->mres_result_path, &statbuf) == 0) {
			if (vperm_chmod_if_simulated_device(realfnname, &statbuf, mode,
					suid_sgid_bits) == 0)
				return(0);
			/* else not a simulated device, continue here */
		} /* else the real function sets errno */
	}

	res = (*real_chmod_ptr)(mapped_filename->mres_result_path, mode);
	e = errno;

	/* need to update vperm state if
	 *  - SUID/SGID is used
	 *  - real function failed due to permissions
	 *  - real function succeeded and there are active vperms.
	 * Don't need to update vperms, if
	 *  - nothing active in vperm tree
	 *  - other errors than EPERM.
	*/
	if (suid_sgid_bits ||
	    ((res < 0) && ( e == EPERM)) ||
	    ((res == 0) && (get_vperm_num_active_inodestats() > 0))) {
		if (real_stat(mapped_filename->mres_result_path, &statbuf) == 0) {
			vperm_chmod(realfnname, &statbuf, mode, suid_sgid_bits);
			res = 0;
		} else { /* real fn didn't work, and can't stat */
			*result_errno_ptr = e;
			res = -1;
		}
	}

	SB_LOG(SB_LOGLEVEL_DEBUG, "%s: returns %d", __func__, res);
	return(res);
}

/* ======================= mknod() variants ======================= */

/* create a dummy, simulated
 * device node: First create a file to get an
 * inode, then set it to be simulated.
 * (Races are possible (breveen creat() and 
 * vperm_mknod(), but can't be avoided)
*/
static int vperm_mknod(
	int dirfd,
	int *result_errno_ptr,
	const mapping_results_t *mapped_filename,
	mode_t mode,
	dev_t dev)
{
	struct stat statbuf;
	int dummy_dev_fd;
	int res;

	/* we can only simulate device nodes. */
	switch (mode & S_IFMT) {
	case S_IFCHR:
		SB_LOG(SB_LOGLEVEL_DEBUG, "%s: Creating simulated character device node", __func__);
		break;
	case S_IFBLK:
		SB_LOG(SB_LOGLEVEL_DEBUG, "%s: Creating simulated block device node", __func__);
		break;
	default:
		SB_LOG(SB_LOGLEVEL_DEBUG,
			"%s: not a device node, can't simulate (type=0%o)",
			__func__, (mode & S_IFMT));
		*result_errno_ptr = EPERM;
                return(-1);
	}

	/* Create a file without read or write permissions;
	 * the simulated device should not be used ever, and
	 * assuming that the user is not privileged,
	 * it can't be opened...which is exactly what we want.
	*/
	dummy_dev_fd = openat_nomap_nolog(dirfd,
		mapped_filename->mres_result_path, O_CREAT|O_WRONLY|O_TRUNC, 0000);
	if (dummy_dev_fd < 0) {
		SB_LOG(SB_LOGLEVEL_DEBUG, "%s: failed to create as a file (%s)",
			 __func__, mapped_filename->mres_result_path);
		*result_errno_ptr = EPERM;
		return(-1);
	} 

	res = real_fstat(dummy_dev_fd, &statbuf);
	close_nomap_nolog(dummy_dev_fd);
	if (res == 0) {
		ruletree_rpc__vperm_set_dev_node((uint64_t)statbuf.st_dev,
			(uint64_t)statbuf.st_ino, mode, (uint64_t)dev);
	} else {
		*result_errno_ptr = EPERM;
		res = -1;
	}
	return(res);
}

int __xmknod_gate(int *result_errno_ptr,
	int (*real___xmknod_ptr)(int ver, const char *path, mode_t mode, dev_t *dev),
        const char *realfnname,
	int ver,
	const mapping_results_t *mapped_filename,
	mode_t mode,
	dev_t *dev)
{
	int	res;

	/* first try the real function */
	res = (*real___xmknod_ptr)(ver, mapped_filename->mres_result_path, mode, dev);
	if (res == 0) {
		/* OK, success. */
		/* If this inode has been virtualized, update DB */
		if (get_vperm_num_active_inodestats() > 0) {
			struct stat statbuf;
			/* there are active vperm inodestat nodes */
			if (real_stat(mapped_filename->mres_result_path, &statbuf) == 0) {
				/* since the real function succeeds, use the real inode
				 * directly and release all simulated information */
				vperm_clear_all_if_virtualized(realfnname, &statbuf);
			}
		}
	} else {
		int	e = errno;

		if (e == EPERM) {
			res = vperm_mknod(AT_FDCWD, result_errno_ptr,
				mapped_filename, mode, *dev);
		} else {
			*result_errno_ptr = errno;
		}
	}
	SB_LOG(SB_LOGLEVEL_DEBUG, "%s: returns %d", __func__, res);
	return(res);
}

int __xmknodat_gate(int *result_errno_ptr,
	int (*real___xmknodat_ptr)(int ver, int dirfd, const char *pathname, mode_t mode, dev_t *dev),
        const char *realfnname,
	int ver,
	int dirfd,
	const mapping_results_t *mapped_filename,
	mode_t mode, dev_t *dev)
{
	int	res;

	/* first try the real function */
	res = (*real___xmknodat_ptr)(ver, dirfd, mapped_filename->mres_result_path, mode, dev);
	if (res == 0) {
		/* OK, success. */
		/* If this inode has been virtualized, update DB */
		if (get_vperm_num_active_inodestats() > 0) {
			struct stat statbuf;
			/* there are active vperm inodestat nodes */
			if (get_stat_for_fxxat(realfnname, dirfd, mapped_filename, 0, &statbuf) == 0) {
				/* since the real function succeeds, use the real inode
				 * directly and release all simulated information */
				vperm_clear_all_if_virtualized(realfnname, &statbuf);
			}
		}
	} else {
		int	e = errno;

		if (e == EPERM) {
			res = vperm_mknod(dirfd, result_errno_ptr,
				mapped_filename, mode, *dev);
		} else {
			*result_errno_ptr = errno;
		}
	}
	SB_LOG(SB_LOGLEVEL_DEBUG, "%s: returns %d", __func__, res);
	return(res);
}


/* ======================= unlink() variants, remove(), rmdir() ======================= */
int unlink_gate(int *result_errno_ptr,
	int (*real_unlink_ptr)(const char *pathname),
        const char *realfnname,
	const mapping_results_t *mapped_filename)
{
	int has_stat = 0;
	struct stat statbuf;
	int res;

	/* If this inode has been virtualized, be prepared to update DB */
	if (get_vperm_num_active_inodestats() > 0) {
		/* there are active vperm inodestat nodes */
		if (real_stat(mapped_filename->mres_result_path, &statbuf) == 0) {
			has_stat = 1;
		}
	}

	res = (*real_unlink_ptr)(mapped_filename->mres_result_path);
	if (res == 0) {
		if (has_stat) {
			/* FIXME: races are possible here. Might be better if
			 * sb2d unlinks the file ? */
			if (statbuf.st_nlink == 1)
				vperm_clear_all_if_virtualized(realfnname, &statbuf);
		}
	} else {
		*result_errno_ptr = errno;
	}
	return(res);
}

int remove_gate(int *result_errno_ptr,
	int (*real_remove_ptr)(const char *pathname),
        const char *realfnname,
	const mapping_results_t *mapped_filename)
{
	int has_stat = 0;
	struct stat statbuf;
	int res;

	/* If this inode has been virtualized, be prepared to update DB */
	if (get_vperm_num_active_inodestats() > 0) {
		/* there are active vperm inodestat nodes */
		if (real_stat(mapped_filename->mres_result_path, &statbuf) == 0) {
			has_stat = 1;
			/* FIXME. Build handle and check if it is DB, don't use IPC every time */
		}
	}

	res = (*real_remove_ptr)(mapped_filename->mres_result_path);
	if (res == 0) {
		if (has_stat) {
			/* FIXME: races are possible here. Might be better if
			 * sb2d removes the file ? */
			if (statbuf.st_nlink == 1)
				vperm_clear_all_if_virtualized(realfnname, &statbuf);
		}
	} else {
		*result_errno_ptr = errno;
	}
	return(res);
}

int rmdir_gate(int *result_errno_ptr,
	int (*real_rmdir_ptr)(const char *pathname),
        const char *realfnname,
	const mapping_results_t *mapped_filename)
{
	int has_stat = 0;
	struct stat statbuf;
	int res;

	/* If this inode has been virtualized, be prepared to update DB */
	if (get_vperm_num_active_inodestats() > 0) {
		/* there are active vperm inodestat nodes */
		if (real_stat(mapped_filename->mres_result_path, &statbuf) == 0) {
			has_stat = 1;
			/* FIXME. Build handle and check if it is DB, don't use IPC every time */
		}
	}

	res = (*real_rmdir_ptr)(mapped_filename->mres_result_path);
	if (res == 0) {
		if (has_stat) {
			/* FIXME: races are possible here. Might be better if
			 * sb2d removes the file ? */
			vperm_clear_all_if_virtualized(realfnname, &statbuf);
		}
	} else {
		*result_errno_ptr = errno;
	}
	return(res);
}

int unlinkat_gate(int *result_errno_ptr,
	int (*real_unlinkat_ptr)(int dirfd, const char *pathname, int flags),
        const char *realfnname,
	int dirfd,
	const mapping_results_t *mapped_filename,
	int flags)
{
	int has_stat = 0;
	struct stat statbuf;
	int res;

	/* If this inode has been virtualized, be prepared to update DB */
	if (get_vperm_num_active_inodestats() > 0) {
		/* there are active vperm inodestat nodes */
		if (get_stat_for_fxxat(realfnname, dirfd, mapped_filename, 0, &statbuf) == 0) {
			has_stat = 1;
		}
	}

	res = (*real_unlinkat_ptr)(dirfd, mapped_filename->mres_result_path, flags);
	if (res == 0) {
		if (has_stat) {
			/* FIXME: races are possible here. Might be better if
			 * sb2d does the operation ? */
			if (statbuf.st_nlink == 1)
				vperm_clear_all_if_virtualized(realfnname, &statbuf);
		}
	} else {
		*result_errno_ptr = errno;
	}
	return(res);
}

/* ======================= mkdir() variants ======================= */

static void vperm_set_owner_and_group(
	int dirfd,
	const char *realfnname,
	const mapping_results_t *mapped_pathname)
{
	struct stat statbuf;

	if (real_fstatat(dirfd, mapped_pathname->mres_result_path, &statbuf, 0) == 0) {
		vperm_chown(realfnname, &statbuf, vperm_geteuid(), vperm_getegid());
	} else {
		SB_LOG(SB_LOGLEVEL_DEBUG, "%s: stat failed", realfnname);
	}
}

int mkdir_gate(int *result_errno_ptr,
	int (*real_mkdir_ptr)(const char *pathname, mode_t mode),
        const char *realfnname,
	const mapping_results_t *mapped_pathname,
	mode_t mode)
{
	int res;
	int e;

	res = (*real_mkdir_ptr)(mapped_pathname->mres_result_path, mode);
	e = errno;

	if (res == 0) {
		/* directory was created */
		if (vperm_uid_or_gid_virtualization_is_active())
			vperm_set_owner_and_group(AT_FDCWD, realfnname, mapped_pathname);
	} else {
		*result_errno_ptr = e;
	}
	return (res);
}

int mkdirat_gate(int *result_errno_ptr,
	int (*real_mkdirat_ptr)(int dirfd, const char *pathname, mode_t mode),
        const char *realfnname,
	int dirfd,
	const mapping_results_t *mapped_pathname,
	mode_t mode)
{
	int res;
	int e;

	res = (*real_mkdirat_ptr)(dirfd, mapped_pathname->mres_result_path, mode);
	e = errno;

	if (res == 0) {
		/* directory was created */
		if (vperm_uid_or_gid_virtualization_is_active())
			vperm_set_owner_and_group(AT_FDCWD, realfnname, mapped_pathname);
	} else {
		*result_errno_ptr = e;
	}
	return (res);
}

/* ======================= open() and variants ======================= */
/* open() etc typically ony need to set simulated owner, if the file
 * was created.
*/

typedef struct open_state_s {
	int target_exists_beforehand;
	int uid_or_gid_is_virtual;
} open_state_t;

#define PREPARE_OPEN(open_state, dirfd, mapped_pathname) do { \
		(open_state)->target_exists_beforehand = -1; \
		(open_state)->uid_or_gid_is_virtual = vperm_uid_or_gid_virtualization_is_active(); \
		if ((open_state)->uid_or_gid_is_virtual) { \
			(open_state)->target_exists_beforehand = (faccessat_nomap_nolog((dirfd), \
				(mapped_pathname)->mres_result_path, F_OK, 0) == 0); \
		} \
	} while(0)

#define FINALIZE_OPEN(open_state, result_errno_ptr, realfnname, dirfd, mapped_pathname, res) do { \
		if ((res) < 0) { \
			*(result_errno_ptr) = errno; \
		} else { \
			if ((open_state)->uid_or_gid_is_virtual && \
			    ((open_state)->target_exists_beforehand==0)) { \
				/* file was created */ \
				vperm_set_owner_and_group((dirfd), (realfnname), (mapped_pathname)); \
			} \
		} \
	} while(0)

int open_gate(int *result_errno_ptr,
	int (*real_open_ptr)(const char *pathname, int flags, ...),
        const char *realfnname,
	const mapping_results_t *mapped_pathname,
	int flags,
	int mode)
{
	int res;
	open_state_t openst;

	PREPARE_OPEN(&openst, AT_FDCWD, mapped_pathname);
	res = (*real_open_ptr)(mapped_pathname->mres_result_path, flags, mode);
	FINALIZE_OPEN(&openst, result_errno_ptr, realfnname, AT_FDCWD, mapped_pathname, res);
	return(res);
}

int open64_gate(int *result_errno_ptr,
	int (*real_open64_ptr)(const char *pathname, int flags, ...),
        const char *realfnname,
	const mapping_results_t *mapped_pathname,
	int flags,
	int mode)
{
	int res;
	open_state_t openst;

	PREPARE_OPEN(&openst, AT_FDCWD, mapped_pathname);
	res = (*real_open64_ptr)(mapped_pathname->mres_result_path, flags, mode);
	FINALIZE_OPEN(&openst, result_errno_ptr, realfnname, AT_FDCWD, mapped_pathname, res);
	return(res);
}

int __open_gate(int *result_errno_ptr,
	int (*real_open_ptr)(const char *pathname, int flags, ...),
        const char *realfnname,
	const mapping_results_t *mapped_pathname,
	int flags,
	int mode)
{
	int res;
	open_state_t openst;

	PREPARE_OPEN(&openst, AT_FDCWD, mapped_pathname);
	res = (*real_open_ptr)(mapped_pathname->mres_result_path, flags, mode);
	FINALIZE_OPEN(&openst, result_errno_ptr, realfnname, AT_FDCWD, mapped_pathname, res);
	return(res);
}

int __open64_gate(int *result_errno_ptr,
	int (*real_open64_ptr)(const char *pathname, int flags, ...),
        const char *realfnname,
	const mapping_results_t *mapped_pathname,
	int flags,
	int mode)
{
	int res;
	open_state_t openst;

	PREPARE_OPEN(&openst, AT_FDCWD, mapped_pathname);
	res = (*real_open64_ptr)(mapped_pathname->mres_result_path, flags, mode);
	FINALIZE_OPEN(&openst, result_errno_ptr, realfnname, AT_FDCWD, mapped_pathname, res);
	return(res);
}

int openat_gate(int *result_errno_ptr,
	int (*real_openat_ptr)(int dirfd, const char *pathname, int flags, ...),
        const char *realfnname,
	int dirfd,
	const mapping_results_t *mapped_pathname,
	int flags,
	int mode)
{
	int res;
	open_state_t openst;

	PREPARE_OPEN(&openst, dirfd, mapped_pathname);
	res = (*real_openat_ptr)(dirfd, mapped_pathname->mres_result_path, flags, mode);
	FINALIZE_OPEN(&openst, result_errno_ptr, realfnname, dirfd, mapped_pathname, res);
	return(res);
}

int openat64_gate(int *result_errno_ptr,
	int (*real_openat64_ptr)(int dirfd, const char *pathname, int flags, ...),
        const char *realfnname,
	int dirfd,
	const mapping_results_t *mapped_pathname,
	int flags,
	int mode)
{
	int res;
	open_state_t openst;

	PREPARE_OPEN(&openst, AT_FDCWD, mapped_pathname);
	res = (*real_openat64_ptr)(dirfd, mapped_pathname->mres_result_path, flags, mode);
	FINALIZE_OPEN(&openst, result_errno_ptr, realfnname, AT_FDCWD, mapped_pathname, res);
	return(res);
}

int creat_gate(int *result_errno_ptr,
	int (*real_creat_ptr)(const char *pathname, mode_t mode),
        const char *realfnname,
	const mapping_results_t *mapped_pathname,
	mode_t mode)
{
	int res;
	open_state_t openst;

	PREPARE_OPEN(&openst, AT_FDCWD, mapped_pathname);
	res = (*real_creat_ptr)(mapped_pathname->mres_result_path, mode);
	FINALIZE_OPEN(&openst, result_errno_ptr, realfnname, AT_FDCWD, mapped_pathname, res);
	return(res);
}

int creat64_gate(int *result_errno_ptr,
	int (*real_creat64_ptr)(const char *pathname, mode_t mode),
        const char *realfnname,
	const mapping_results_t *mapped_pathname,
	mode_t mode)
{
	int res;
	open_state_t openst;

	PREPARE_OPEN(&openst, AT_FDCWD, mapped_pathname);
	res = (*real_creat64_ptr)(mapped_pathname->mres_result_path, mode);
	FINALIZE_OPEN(&openst, result_errno_ptr, realfnname, AT_FDCWD, mapped_pathname, res);
	return(res);
}

#define PREPARE_FOPEN(open_state, mapped_pathname) do { \
		(open_state)->target_exists_beforehand = -1; \
		(open_state)->uid_or_gid_is_virtual = vperm_uid_or_gid_virtualization_is_active(); \
		if ((open_state)->uid_or_gid_is_virtual) { \
			(open_state)->target_exists_beforehand = (faccessat_nomap_nolog((AT_FDCWD), \
				(mapped_pathname)->mres_result_path, F_OK, 0) == 0); \
		} \
	} while(0)

#define FINALIZE_FOPEN(open_state, result_errno_ptr, realfnname, mapped_pathname, res) do { \
		if ((res) == NULL) { \
			*(result_errno_ptr) = errno; \
		} else { \
			if ((open_state)->uid_or_gid_is_virtual && \
			    ((open_state)->target_exists_beforehand==0)) { \
				/* file was created */ \
				vperm_set_owner_and_group((AT_FDCWD), (realfnname), (mapped_pathname)); \
			} \
		} \
	} while(0)

FILE * fopen_gate(int *result_errno_ptr,
	FILE *(*real_fopen_ptr)(const char *path, const char *mode),
        const char *realfnname,
	const mapping_results_t *mapped_pathname,
	const char *mode)
{
	FILE *res;
	open_state_t openst;

	PREPARE_FOPEN(&openst, mapped_pathname);
	res = (*real_fopen_ptr)(mapped_pathname->mres_result_path, mode);
	FINALIZE_FOPEN(&openst, result_errno_ptr, realfnname, mapped_pathname, res);
	return(res);
}

FILE * fopen64_gate(int *result_errno_ptr,
	FILE *(*real_fopen64_ptr)(const char *path, const char *mode),
        const char *realfnname,
	const mapping_results_t *mapped_pathname,
	const char *mode)
{
	FILE *res;
	open_state_t openst;

	PREPARE_FOPEN(&openst, mapped_pathname);
	res = (*real_fopen64_ptr)(mapped_pathname->mres_result_path, mode);
	FINALIZE_FOPEN(&openst, result_errno_ptr, realfnname, mapped_pathname, res);
	return(res);
}

FILE * freopen_gate(int *result_errno_ptr,
	FILE *(*real_freopen_ptr)(const char *path, const char *mode, FILE *stream),
        const char *realfnname,
	const mapping_results_t *mapped_pathname,
	const char *mode,
	FILE *stream)
{
	FILE *res;
	open_state_t openst;

	PREPARE_FOPEN(&openst, mapped_pathname);
	res = (*real_freopen_ptr)(mapped_pathname->mres_result_path, mode, stream);
	FINALIZE_FOPEN(&openst, result_errno_ptr, realfnname, mapped_pathname, res);
	return(res);
}

FILE * freopen64_gate(int *result_errno_ptr,
	FILE *(*real_freopen64_ptr)(const char *path, const char *mode, FILE *stream),
        const char *realfnname,
	const mapping_results_t *mapped_pathname,
	const char *mode,
	FILE *stream)
{
	FILE *res;
	open_state_t openst;

	PREPARE_FOPEN(&openst, mapped_pathname);
	res = (*real_freopen64_ptr)(mapped_pathname->mres_result_path, mode, stream);
	FINALIZE_FOPEN(&openst, result_errno_ptr, realfnname, mapped_pathname, res);
	return(res);
}

/* ======================= rename(), renameat() ======================= */
/* rename may remove a file as a side effect */

int rename_gate(int *result_errno_ptr,
	int (*real_rename_ptr)(const char *oldpath, const char *newpath),
        const char *realfnname,
	const mapping_results_t *mapped_oldpath,
	const mapping_results_t *mapped_newpath)
{
	int has_stat = 0;
	struct stat statbuf;
	int res;

	/* If newpath has been virtualized, be prepared to update DB */
	if (get_vperm_num_active_inodestats() > 0) {
		/* there are active vperm inodestat nodes */
		if (real_lstat(mapped_newpath->mres_result_path, &statbuf) == 0) {
			has_stat = 1;
		}
	}

	res = (*real_rename_ptr)(mapped_oldpath->mres_result_path, mapped_newpath->mres_result_path);
	if (res == 0) {
		if (has_stat) {
			/* FIXME: races are possible here. Might be better if
			 * sb2d does the operation ? */
			/* if it was a file, it was removed if there were 1 links.
			 * but minumum link count for directories is 2.
			*/
			if ((statbuf.st_nlink == 1) ||
			    (S_ISDIR(statbuf.st_mode) && (statbuf.st_nlink == 2))) {
				vperm_clear_all_if_virtualized(realfnname, &statbuf);
			}
		}
	} else {
		*result_errno_ptr = errno;
	}
	return(res);
}

int renameat_gate(int *result_errno_ptr,
	int (*real_renameat_ptr)(int olddirfd, const char *oldpath, int newdirfd, const char *newpath),
        const char *realfnname,
	int olddirfd,
	const mapping_results_t *mapped_oldpath,
	int newdirfd,
	const mapping_results_t *mapped_newpath)
{
	int has_stat = 0;
	struct stat statbuf;
	int res;

	/* If newpath has been virtualized, be prepared to update DB */
	if (get_vperm_num_active_inodestats() > 0) {
		/* there are active vperm inodestat nodes */
		if (get_stat_for_fxxat(realfnname, newdirfd, mapped_newpath, AT_SYMLINK_NOFOLLOW, &statbuf) == 0) {
			has_stat = 1;
		}
	}

	res = (*real_renameat_ptr)(olddirfd, mapped_oldpath->mres_result_path,
		newdirfd, mapped_newpath->mres_result_path);
	if (res == 0) {
		if (has_stat) {
			/* FIXME: races are possible here. Might be better if
			 * sb2d does the operation ? */
			/* if it was a file, it was removed if there were 1 links.
			 * but minumum link count for directories is 2.
			*/
			if ((statbuf.st_nlink == 1) ||
			    (S_ISDIR(statbuf.st_mode) && (statbuf.st_nlink == 2))) {
				vperm_clear_all_if_virtualized(realfnname, &statbuf);
			}
		}
	} else {
		*result_errno_ptr = errno;
	}
	return(res);
}

/* ======================= fts_*() functions dealing with stat structures ======================= */

#ifdef HAVE_FTS_H
FTSENT *fts_read_gate(int *result_errno_ptr,
	FTSENT * (*real_fts_read_ptr)(FTS *ftsp),
        const char *realfnname,
	FTS *ftsp)
{
	FTSENT *res;

	res = (*real_fts_read_ptr)(ftsp);
	if (res && (res->fts_statp)) {
		i_virtualize_struct_stat(res->fts_statp, NULL);
	} else if (res==NULL) {
		*result_errno_ptr = errno;
	}
	return(res);
}

FTSENT *fts_children_gate(int *result_errno_ptr,
	FTSENT * (*real_fts_children_ptr)(FTS *ftsp, int options),
        const char *realfnname,
	FTS *ftsp,
	int options)
{
	FTSENT *res;

	res = (*real_fts_children_ptr)(ftsp, options);

	/* FIXME: check the "options" condition from glibc */
	if (res && (options != FTS_NAMEONLY) && (get_vperm_num_active_inodestats() > 0)) {
		FTSENT *fep = res;
		while (fep) {
			if (fep->fts_statp)
				i_virtualize_struct_stat(fep->fts_statp, NULL);
			fep = fep->fts_link;
		}
	} else if (res==NULL) {
		*result_errno_ptr = errno;
	}
	return(res);
}
#endif /* HAVE_FTS_H */

