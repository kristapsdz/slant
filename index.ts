/* vim: set filetype=javascript: */

namespace index 
{
	function clicked(event: Event): void
	{
		let target: HTMLElement|null = 
			<HTMLElement>event.target;
		while (target !== null && 
		       ! target.classList.contains('popup'))
			target = <HTMLElement>target.parentNode;
		if (target !== null)
			target.classList.toggle('shown');
	}

	export function init(): void
	{
		let list: NodeListOf<Element>;
		let i: number;
		list = document.getElementsByClassName('popup');
		for (i = 0; i < list.length; i++)
			(<HTMLElement>list[i]).onclick = clicked;
	}
};

document.addEventListener('DOMContentLoaded', index.init);
